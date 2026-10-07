#include "transcription_session.h"

#include <QDateTime>
#include <QMetaObject>
#include <exception>
#include <utility>

namespace pcm::calltranscription {

using pcm::transcription::TrackInfo;
using pcm::transcription::TrackRole;
using pcm::transcription::TranscribedPhrase;
using pcm::transcription::TranscriptionEngine;

namespace {
constexpr auto kGracefulTimeout = std::chrono::seconds(5);
constexpr int kMaxConsecutiveStoreFailures = 3;
}  // namespace

TranscriptionSession::TranscriptionSession(std::shared_ptr<pcm::database::Database> db,
                                           pcm::video::VideoProvider *provider,
                                           EngineFactory factory, QObject *parent)
    : QObject(parent), db_(std::move(db)), provider_(provider), factory_(std::move(factory)) {
  qRegisterMetaType<SessionState>();
  qRegisterMetaType<DuckTranscriptPhrase>();
  if (provider_) {
    connect(provider_, &pcm::video::VideoProvider::participantJoined, this,
            &TranscriptionSession::onParticipantJoined);
    connect(provider_, &pcm::video::VideoProvider::participantLeft, this,
            [this](const QString &id) {
              if (state() == SessionState::Recording) removeTrackFor(id);
            });
    connect(provider_, &pcm::video::VideoProvider::left, this, [this] { stop(); });
    connect(provider_, &pcm::video::VideoProvider::connectionLost, this,
            [this](const QString &) { stop(); });
  }
}

TranscriptionSession::~TranscriptionSession() {
  const auto current = state();
  const bool active = current == SessionState::Loading || current == SessionState::Recording ||
                      current == SessionState::Stopping;
  alive_->store(false);
  detachSink();
  if (!active) return;  // nothing running; executor_ drains nothing
  // Hard stop: abort engine and writer without waiting for decoding, then join.
  hardStop_ = true;
  executor_.post([this] {
    if (auto e = takeEngine()) e->stop(std::chrono::milliseconds(0));
    if (writer_) writer_->stop(std::chrono::milliseconds(0));
  });
  executor_.waitIdle();
  if (writer_) writer_->stop(std::chrono::milliseconds(0));
  try {
    const auto id = transcriptId_.load();
    if (id > 0) {
      if (revokeRequested_)
        db_->revoke_transcript_consent(id);
      else
        db_->set_transcript_status(id, "draft");
    }
  } catch (...) {
  }
}

void TranscriptionSession::setState(SessionState s) {
  if (state_.exchange(s) != s) emit stateChanged(s);
}

void TranscriptionSession::postGui(std::function<void()> fn) {
  QMetaObject::invokeMethod(
      this,
      [alive = alive_, fn = std::move(fn)] {
        if (alive->load()) fn();
      },
      Qt::QueuedConnection);
}

void TranscriptionSession::detachSink() {
  if (proxy_) proxy_->detach();
  if (provider_) provider_->setAudioSink(nullptr);
}

int64_t TranscriptionSession::callClockMs() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                               clockStart_)
      .count();
}

std::shared_ptr<TranscriptionEngine> TranscriptionSession::engine() const {
  std::lock_guard lock(engineMutex_);
  return engine_;
}

std::shared_ptr<TranscriptionEngine> TranscriptionSession::takeEngine() {
  std::lock_guard lock(engineMutex_);
  return std::exchange(engine_, nullptr);
}

bool TranscriptionSession::start(int64_t eventId, const QString &consentScope) {
  if (state() != SessionState::Idle || eventId <= 0 || consentScope.isEmpty() || !db_ ||
      !provider_ || !factory_)
    return false;

  int64_t id = 0;
  try {
    id = db_->add_transcript(eventId, consentScope.toStdString(), std::nullopt,
                             QDateTime::currentMSecsSinceEpoch());
  } catch (...) {
    return false;
  }
  if (id <= 0) return false;
  transcriptId_ = id;
  clockStart_ = std::chrono::steady_clock::now();

  writer_ = std::make_shared<PhraseWriter>(
      [this](const DuckTranscriptPhrase &phrase) -> int64_t {
        int64_t rowId = 0;
        try {
          rowId = db_->add_transcript_phrase(phrase);
        } catch (...) {
        }
        if (rowId > 0) {
          consecutiveStoreFailures_ = 0;
        } else if (++consecutiveStoreFailures_ >= kMaxConsecutiveStoreFailures &&
                   !storeFailureReported_.exchange(true)) {
          postGui([this] { emit failed(QStringLiteral("Could not save the transcript")); });
        }
        return rowId;
      },
      [this](const DuckTranscriptPhrase &row) {
        postGui([this, row] { emit phraseAdded(row); });
      });

  proxy_ = std::make_shared<SinkProxy>(this);
  provider_->setAudioSink(proxy_);
  setState(SessionState::Loading);

  // Callbacks run on the engine's decode thread. They touch only the writer and
  // the GUI queue; the engine is stopped and joined before this object dies.
  pcm::transcription::EngineCallbacks callbacks;
  callbacks.on_phrase = [writer = writer_, id](const TranscribedPhrase &p) {
    DuckTranscriptPhrase row;
    row.transcript_id = id;
    row.track_role = p.role == TrackRole::Practitioner ? "practitioner" : "participant";
    if (!p.speaker_name.empty()) row.speaker_name = p.speaker_name;
    row.start_ms = p.start_ms;
    row.end_ms = p.end_ms;
    row.text = p.text;
    writer->submit(std::move(row));
  };
  callbacks.on_delayed = [this](bool delayed) {
    postGui([this, delayed] { emit delayedChanged(delayed); });
  };

  executor_.post([this, callbacks = std::move(callbacks)]() mutable {
    QString error;
    std::shared_ptr<TranscriptionEngine> created;
    try {
      created = factory_(std::move(callbacks), &error);
    } catch (const std::exception &e) {
      error = QString::fromUtf8(e.what());
    } catch (...) {
      error = QStringLiteral("Unknown error while loading the transcription model");
    }
    if (!created) {
      writer_->stop(std::chrono::milliseconds(0));
      postGui([this, error] { onLoadFailed(error); });
      return;
    }
    {
      std::lock_guard lock(engineMutex_);
      engine_ = std::move(created);
    }
    postGui([this] { onEngineReady(); });
  });
  return true;
}

void TranscriptionSession::onLoadFailed(const QString &reason) {
  if (state() != SessionState::Loading) return;  // a stop/revoke is finalising
  detachSink();
  try {
    db_->set_transcript_status(transcriptId_, "draft");
  } catch (...) {
  }
  setState(SessionState::Failed);
  emit failed(reason.isEmpty() ? QStringLiteral("Could not load the transcription model")
                               : reason);
}

void TranscriptionSession::onEngineReady() {
  if (state() != SessionState::Loading) return;
  setState(SessionState::Recording);
  // Everyone present now (including those who joined while loading) starts
  // delivering audio from this moment, so the offset is the current call clock.
  auto *model = provider_ ? provider_->participants() : nullptr;
  if (!model) return;
  const auto offset = callClockMs();
  for (int row = 0; row < model->rowCount(); ++row) {
    const auto id =
        model->data(model->index(row, 0), pcm::video::ParticipantModel::IdRole).toString();
    addTrackFor(id, offset);
  }
}

void TranscriptionSession::onParticipantJoined(const QString &id) {
  // During Loading the participant is picked up from the model in onEngineReady().
  if (state() != SessionState::Recording) return;
  addTrackFor(id, callClockMs());
}

void TranscriptionSession::addTrackFor(const QString &id, int64_t offsetMs) {
  const auto participant = provider_ ? provider_->participants()->participant(id) : std::nullopt;
  if (!participant) return;
  TrackInfo info{id.toStdString(),
                 participant->isLocal ? TrackRole::Practitioner : TrackRole::Participant,
                 participant->displayName.toStdString()};
  executor_.post([this, info = std::move(info), offsetMs] {
    if (auto e = engine()) e->addTrack(info, offsetMs);
  });
}

void TranscriptionSession::removeTrackFor(const QString &id) {
  executor_.post([this, trackId = id.toStdString()] {
    if (auto e = engine()) e->removeTrack(trackId);
  });
}

void TranscriptionSession::onAudio(const QString &participantId, const int16_t *samples,
                                   std::size_t count, int sampleRate) {
  if (auto e = engine()) e->pushAudio(participantId.toStdString(), samples, count, sampleRate);
}

void TranscriptionSession::beginShutdown(bool graceful) {
  const auto s = state();
  if (s != SessionState::Loading && s != SessionState::Recording) return;
  setState(SessionState::Stopping);
  detachSink();
  revokeRequested_ = !graceful;
  const auto timeout = graceful ? std::chrono::milliseconds(kGracefulTimeout)
                                : std::chrono::milliseconds(0);
  executor_.post([this, graceful, timeout] {
    const auto t = hardStop_ ? std::chrono::milliseconds(0) : timeout;
    if (auto e = takeEngine()) e->stop(t);
    writer_->stop(t);
    postGui([this, graceful] {
      const auto id = transcriptId_.load();
      try {
        if (graceful)
          db_->set_transcript_status(id, "draft");
        else
          db_->revoke_transcript_consent(id);
      } catch (...) {
      }
      setState(SessionState::Finished);
      if (graceful)
        emit finished(id);
      else
        emit revoked(id);
    });
  });
}

void TranscriptionSession::stop() { beginShutdown(true); }

void TranscriptionSession::revoke() { beginShutdown(false); }

}  // namespace pcm::calltranscription
