#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

#include "audio_sink.h"
#include "database.h"
#include "phrase_writer.h"
#include "schema.hpp"
#include "serial_executor.h"
#include "transcription_engine.h"
#include "video_provider.h"

namespace pcm::calltranscription {

// Runs on the executor thread: loads the model/VAD and builds the engine with
// the given callbacks. Returns null and sets *error on failure.
using EngineFactory = std::function<std::shared_ptr<pcm::transcription::TranscriptionEngine>(
    pcm::transcription::EngineCallbacks callbacks, QString *error)>;

enum class SessionState { Idle, Loading, Recording, Stopping, Finished, Failed };

// Consent-gated live transcription of one call. GUI-thread object; the model
// is loaded, fed track changes and stopped on its own executor thread, phrases
// are persisted on a dedicated writer thread. No sink is installed on the
// provider and no engine exists unless start() succeeded.
class TranscriptionSession final : public QObject, public pcm::video::AudioSink {
  Q_OBJECT
 public:
  TranscriptionSession(std::shared_ptr<pcm::database::Database> db,
                       pcm::video::VideoProvider *provider, EngineFactory factory,
                       QObject *parent = nullptr);
  ~TranscriptionSession() override;

  [[nodiscard]] SessionState state() const { return state_.load(); }
  [[nodiscard]] int64_t transcriptId() const { return transcriptId_.load(); }

  // GUI thread. Requires state()==Idle, eventId>0, non-empty consentScope.
  bool start(int64_t eventId, const QString &consentScope);
  // Graceful: drains the engine and writer, status "draft", emits finished.
  void stop();
  // No drain: aborts engine and writer, revokes consent, emits revoked.
  void revoke();

  void onAudio(const QString &participantId, const int16_t *samples, std::size_t count,
               int sampleRate) override;

 signals:
  void stateChanged(pcm::calltranscription::SessionState state);
  void phraseAdded(DuckTranscriptPhrase phrase);
  void delayedChanged(bool delayed);
  void failed(QString reason);
  void finished(qint64 transcriptId);
  void revoked(qint64 transcriptId);

 private:
  // Held by the provider's slot; forwards to the session only while attached.
  class SinkProxy final : public pcm::video::AudioSink {
   public:
    explicit SinkProxy(TranscriptionSession *target) : target_(target) {}
    void onAudio(const QString &id, const int16_t *samples, std::size_t count,
                 int rate) override {
      std::lock_guard lock(mutex_);
      if (target_) target_->onAudio(id, samples, count, rate);
    }
    // Blocks until an in-flight onAudio has returned.
    void detach() {
      std::lock_guard lock(mutex_);
      target_ = nullptr;
    }

   private:
    std::mutex mutex_;
    TranscriptionSession *target_;
  };

  void setState(SessionState state);
  void postGui(std::function<void()> fn);
  void detachSink();
  void addTrackFor(const QString &id, int64_t offsetMs);
  void removeTrackFor(const QString &id);
  [[nodiscard]] int64_t callClockMs() const;
  std::shared_ptr<pcm::transcription::TranscriptionEngine> engine() const;
  std::shared_ptr<pcm::transcription::TranscriptionEngine> takeEngine();
  void onEngineReady();
  void onLoadFailed(const QString &reason);
  void beginShutdown(bool graceful);
  void onParticipantJoined(const QString &id);

  std::shared_ptr<pcm::database::Database> db_;
  QPointer<pcm::video::VideoProvider> provider_;
  EngineFactory factory_;
  std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

  std::atomic<SessionState> state_{SessionState::Idle};
  std::atomic<int64_t> transcriptId_{0};
  std::atomic<bool> hardStop_{false};
  bool revokeRequested_ = false;  // GUI thread
  std::chrono::steady_clock::time_point clockStart_;

  std::shared_ptr<SinkProxy> proxy_;
  std::shared_ptr<PhraseWriter> writer_;
  std::atomic<int> consecutiveStoreFailures_{0};
  std::atomic<bool> storeFailureReported_{false};

  mutable std::mutex engineMutex_;
  std::shared_ptr<pcm::transcription::TranscriptionEngine> engine_;

  // Last member: destroyed first, draining queued tasks while the rest is alive.
  SerialExecutor executor_;
};

}  // namespace pcm::calltranscription

Q_DECLARE_METATYPE(pcm::calltranscription::SessionState)
Q_DECLARE_METATYPE(DuckTranscriptPhrase)
