#include "transcription_session.h"

#include <QDateTime>
#include <QDebug>
#include <QMetaObject>
#include <exception>
#include <map>
#include <thread>
#include <utility>

namespace pcm::calltranscription {

using pcm::transcription::TrackInfo;
using pcm::transcription::TrackRole;
using pcm::transcription::TranscribedPhrase;
using pcm::transcription::TranscriptionEngine;

namespace {
constexpr std::chrono::milliseconds kGracefulTimeout{5000};
constexpr std::chrono::milliseconds kNoWait{0};
constexpr int kMaxConsecutiveStoreFailures = 3;
}  // namespace

// Posts work to the session's QObject from any thread. After detach() (called
// first thing in the destructor) nothing is posted, so worker threads never
// touch a dying QObject.
class GuiBridge {
 public:
  explicit GuiBridge(QObject *target) : target_(target) {}
  void post(std::function<void()> fn) {
    std::lock_guard lock(mutex_);
    if (target_) QMetaObject::invokeMethod(target_, std::move(fn), Qt::QueuedConnection);
  }
  void detach() {
    std::lock_guard lock(mutex_);
    target_ = nullptr;
  }

 private:
  std::mutex mutex_;
  QObject *target_;
};

// Flags shared by the GUI thread and the workers (also captured by engine and
// writer callbacks, so it holds no owning reference back to the engine).
struct SessionControl {
  // Set by the destructor: do not wait for anything, finalise as a draft.
  std::atomic<bool> abort{false};
  // Set by revoke(). Written only under decisionMutex.
  std::atomic<bool> revokeRequested{false};
  // Serialises "store a phrase" and "decide how the row is finalised" against
  // revoke(), so no phrase can be written after revoke() has returned.
  std::mutex decisionMutex;
  bool finalised = false;  // guarded by decisionMutex: the DB row was finalised by the executor
  std::atomic<int> consecutiveStoreFailures{0};
  std::atomic<bool> storeFailureReported{false};
};

struct SessionShared {
  SessionShared(std::shared_ptr<pcm::database::Database> database, EngineFactory engineFactory,
                QObject *owner)
      : db(std::move(database)),
        factory(std::move(engineFactory)),
        bridge(std::make_shared<GuiBridge>(owner)) {}

  std::shared_ptr<pcm::database::Database> db;
  EngineFactory factory;
  std::shared_ptr<GuiBridge> bridge;
  std::shared_ptr<SessionControl> control = std::make_shared<SessionControl>();

  std::atomic<int64_t> transcriptId{0};
  // Written once on the GUI thread before the first executor task is posted.
  std::chrono::steady_clock::time_point clockStart;
  std::shared_ptr<PhraseWriter> writer;  // set in start(), before tasks are posted

  std::mutex engineMutex;
  std::shared_ptr<TranscriptionEngine> engine;

  // participant id -> engine track id of the CURRENT join. Every join gets a
  // fresh "<id>#<n>" engine id because the engine ignores an add for an id that
  // was just removed until its segmenter erased the old track.
  std::mutex tracksMutex;
  std::map<QString, std::string> tracks;
  uint64_t generation = 0;

  // Last member: joined first. Never destroyed on its own thread: whoever drops
  // the last reference does so on the GUI thread or the detached cleanup thread.
  SerialExecutor executor;

  std::shared_ptr<TranscriptionEngine> currentEngine() {
    std::lock_guard lock(engineMutex);
    return engine;
  }
  std::shared_ptr<TranscriptionEngine> takeEngine() {
    std::lock_guard lock(engineMutex);
    return std::exchange(engine, nullptr);
  }
};

namespace {

// Executor thread. Drains (bounded) unless an abort or revoke is requested; a
// revoke arriving while the engine drains cannot interrupt that one call, but
// the Store/on_phrase gates discard everything it produces and the writer then
// gets no wait. Finalises the DB row exactly once, as revoked if requested.
void runShutdown(const std::shared_ptr<SessionShared> &sh) {
  auto &c = *sh->control;
  const auto budget = [&c] {
    return (c.abort.load() || c.revokeRequested.load()) ? kNoWait : kGracefulTimeout;
  };
  if (auto e = sh->takeEngine()) {
    try {
      e->stop(budget());
    } catch (...) {
    }
  }
  if (sh->writer) {
    try {
      sh->writer->stop(budget());
    } catch (...) {
    }
  }
  std::lock_guard lock(c.decisionMutex);
  const auto id = sh->transcriptId.load();
  try {
    if (c.revokeRequested.load())
      sh->db->revoke_transcript_consent(id);
    else
      sh->db->set_transcript_status(id, "draft");
  } catch (...) {
  }
  c.finalised = true;
}

}  // namespace

TranscriptionSession::TranscriptionSession(std::shared_ptr<pcm::database::Database> db,
                                           pcm::video::VideoProvider *provider,
                                           EngineFactory factory, QObject *parent)
    : QObject(parent),
      provider_(provider),
      shared_(std::make_shared<SessionShared>(std::move(db), std::move(factory), this)) {
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
  shared_->bridge->detach();  // from here on no worker thread touches this object
  detachSink();
  const bool active = current == SessionState::Loading || current == SessionState::Recording ||
                      current == SessionState::Stopping;
  if (!active) return;  // executor is idle; dropping shared_ joins it instantly

  // Never wait here: model load or a decode can take seconds. Abort, make sure a
  // shutdown task is queued (a Stopping session already has one) and let a
  // detached thread keep the shared state alive until the executor went idle.
  // The row ends as "draft", or revoked when revoke() had been requested.
  shared_->control->abort = true;
  if (current != SessionState::Stopping)
    shared_->executor.post([sh = shared_] { runShutdown(sh); });
  try {
    std::thread([sh = shared_] { sh->executor.waitIdle(); }).detach();
  } catch (...) {
    shared_->executor.waitIdle();  // cannot spawn a thread: finish synchronously
  }
}

void TranscriptionSession::setState(SessionState s) {
  if (state_.exchange(s) != s) emit stateChanged(s);
}

void TranscriptionSession::detachSink() {
  if (proxy_) proxy_->detach();
  if (provider_) provider_->setAudioSink(nullptr);
}

bool TranscriptionSession::start(int64_t eventId, const QString &consentScope) {
  auto &sh = *shared_;
  if (state() != SessionState::Idle || eventId <= 0 || consentScope.isEmpty() || !sh.db ||
      !provider_ || !sh.factory)
    return false;

  int64_t id = 0;
  try {
    id = sh.db->add_transcript(eventId, consentScope.toStdString(), std::nullopt,
                               QDateTime::currentMSecsSinceEpoch());
  } catch (...) {
    return false;
  }
  if (id <= 0) return false;
  transcriptId_ = id;
  sh.transcriptId = id;
  sh.clockStart = std::chrono::steady_clock::now();

  auto control = sh.control;
  auto bridge = sh.bridge;
  sh.writer = std::make_shared<PhraseWriter>(
      [db = sh.db, control, bridge, this](const DuckTranscriptPhrase &phrase) -> int64_t {
        int64_t rowId = 0;
        {
          // Holding the lock across the insert is what guarantees that nothing
          // is written after revoke() returned.
          std::lock_guard lock(control->decisionMutex);
          if (control->revokeRequested.load() || control->abort.load()) return 0;
          try {
            rowId = db->add_transcript_phrase(phrase);
          } catch (...) {
          }
        }
        if (rowId > 0) {
          control->consecutiveStoreFailures = 0;
        } else if (++control->consecutiveStoreFailures >= kMaxConsecutiveStoreFailures &&
                   !control->storeFailureReported.exchange(true)) {
          bridge->post(
              [this] { emit failed(QStringLiteral("Could not save the transcript")); });
        }
        return rowId;
      },
      [bridge, this](const DuckTranscriptPhrase &row) {
        bridge->post([this, row] { emit phraseAdded(row); });
      });

  proxy_ = std::make_shared<SinkProxy>(this);
  provider_->setAudioSink(proxy_);
  setState(SessionState::Loading);

  // Callbacks run on the engine's decode thread. They touch only the writer,
  // the control flags and the bridge; never this object directly.
  pcm::transcription::EngineCallbacks callbacks;
  callbacks.on_phrase = [writer = sh.writer, control, id](const TranscribedPhrase &p) {
    if (control->revokeRequested.load() || control->abort.load()) return;
    DuckTranscriptPhrase row;
    row.transcript_id = id;
    row.track_role = p.role == TrackRole::Practitioner ? "practitioner" : "participant";
    if (!p.speaker_name.empty()) row.speaker_name = p.speaker_name;
    row.start_ms = p.start_ms;
    row.end_ms = p.end_ms;
    row.text = p.text;
    writer->submit(std::move(row));
  };
  callbacks.on_delayed = [bridge, this](bool delayed) {
    bridge->post([this, delayed] { emit delayedChanged(delayed); });
  };

  sh.executor.post([sh = shared_, this, callbacks = std::move(callbacks)]() mutable {
    QString error;
    std::shared_ptr<TranscriptionEngine> created;
    try {
      created = sh->factory(std::move(callbacks), &error);
    } catch (const std::exception &e) {
      error = QString::fromUtf8(e.what());
    } catch (...) {
      error = QStringLiteral("Unknown error while loading the transcription model");
    }
    if (!created) {
      try {
        sh->writer->stop(kNoWait);
      } catch (...) {
      }
      // If stop()/revoke() already ran, onLoadFailed ignores this and the
      // queued shutdown task finalises the row (Finished, not Failed).
      sh->bridge->post([this, error] { onLoadFailed(error); });
      return;
    }
    {
      std::lock_guard lock(sh->engineMutex);
      sh->engine = std::move(created);
    }
    sh->bridge->post([this] { onEngineReady(); });
  });
  return true;
}

void TranscriptionSession::onLoadFailed(const QString &reason) {
  if (state() != SessionState::Loading) return;  // a stop/revoke is finalising
  detachSink();
  try {
    shared_->db->set_transcript_status(transcriptId_, "draft");
  } catch (...) {
  }
  setState(SessionState::Failed);
  emit failed(reason.isEmpty() ? QStringLiteral("Could not load the transcription model")
                               : reason);
}

void TranscriptionSession::onEngineReady() {
  if (state() != SessionState::Loading) return;
  setState(SessionState::Recording);
  // Everyone present now (including those who joined while loading) gets a track.
  auto *model = provider_ ? provider_->participants() : nullptr;
  if (!model) return;
  for (int row = 0; row < model->rowCount(); ++row)
    addTrackFor(
        model->data(model->index(row, 0), pcm::video::ParticipantModel::IdRole).toString());
}

void TranscriptionSession::onParticipantJoined(const QString &id) {
  // During Loading the participant is picked up from the model in onEngineReady().
  if (state() != SessionState::Recording) return;
  addTrackFor(id);
}

void TranscriptionSession::addTrackFor(const QString &id) {
  auto *model = provider_ ? provider_->participants() : nullptr;
  if (!model) return;
  const auto participant = model->participant(id);
  if (!participant) return;

  std::string engineId;
  {
    std::lock_guard lock(shared_->tracksMutex);
    if (shared_->tracks.count(id)) return;  // duplicate join signal
    engineId = id.toStdString() + "#" + std::to_string(++shared_->generation);
    shared_->tracks.emplace(id, engineId);
  }
  TrackInfo info{engineId,
                 participant->isLocal ? TrackRole::Practitioner : TrackRole::Participant,
                 participant->displayName.toStdString()};
  shared_->executor.post([sh = shared_, this, info = std::move(info), id] {
    auto e = sh->currentEngine();
    if (!e) return;
    // The offset is taken here, right before the track exists, so queueing
    // delay on this thread cannot skew phrase times.
    const auto offsetMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - sh->clockStart)
                              .count();
    try {
      e->addTrack(info, offsetMs);
    } catch (...) {
      {
        std::lock_guard lock(sh->tracksMutex);
        const auto it = sh->tracks.find(id);
        if (it != sh->tracks.end() && it->second == info.id) sh->tracks.erase(it);
      }
      qWarning() << "TranscriptionSession: could not add an audio track for transcript"
                 << sh->transcriptId.load();  // ids only, no participant data
      sh->bridge->post([this, id] { emit trackFailed(id); });
    }
  });
}

void TranscriptionSession::removeTrackFor(const QString &id) {
  std::string engineId;
  {
    std::lock_guard lock(shared_->tracksMutex);
    const auto it = shared_->tracks.find(id);
    if (it == shared_->tracks.end()) return;
    engineId = std::move(it->second);
    shared_->tracks.erase(it);
  }
  shared_->executor.post([sh = shared_, engineId = std::move(engineId)] {
    if (auto e = sh->currentEngine()) {
      try {
        e->removeTrack(engineId);
      } catch (...) {
      }
    }
  });
}

void TranscriptionSession::onAudio(const QString &participantId, const int16_t *samples,
                                   std::size_t count, int sampleRate) {
  std::string engineId;
  {
    std::lock_guard lock(shared_->tracksMutex);
    const auto it = shared_->tracks.find(participantId);
    if (it == shared_->tracks.end()) return;
    engineId = it->second;
  }
  if (auto e = shared_->currentEngine()) e->pushAudio(engineId, samples, count, sampleRate);
}

void TranscriptionSession::beginShutdown(bool graceful) {
  const auto s = state();
  if (s == SessionState::Stopping) {
    if (!graceful) upgradeShutdownToRevoke();
    return;
  }
  if (s != SessionState::Loading && s != SessionState::Recording) return;
  if (!graceful) {
    std::lock_guard lock(shared_->control->decisionMutex);
    shared_->control->revokeRequested = true;
  }
  setState(SessionState::Stopping);
  detachSink();
  shared_->executor.post([sh = shared_, this] {
    runShutdown(sh);
    sh->bridge->post([this] { finishShutdown(); });
  });
}

// revoke() while a graceful stop is already in flight. Either the executor has
// not finalised the row yet (it will read the flag under the same lock and
// revoke it) or it already wrote "draft", in which case the GUI revokes it here.
void TranscriptionSession::upgradeShutdownToRevoke() {
  auto &c = *shared_->control;
  bool alreadyFinalised = false;
  {
    std::lock_guard lock(c.decisionMutex);
    if (c.revokeRequested.load()) return;
    c.revokeRequested = true;
    alreadyFinalised = c.finalised;
  }
  if (!alreadyFinalised) return;
  try {
    shared_->db->revoke_transcript_consent(transcriptId_.load());
  } catch (...) {
  }
}

void TranscriptionSession::finishShutdown() {
  if (state() != SessionState::Stopping) return;
  const auto id = transcriptId_.load();
  const bool revoked = shared_->control->revokeRequested.load();
  setState(SessionState::Finished);
  if (revoked)
    emit this->revoked(id);
  else
    emit finished(id);
}

void TranscriptionSession::stop() { beginShutdown(true); }

void TranscriptionSession::revoke() { beginShutdown(false); }

}  // namespace pcm::calltranscription
