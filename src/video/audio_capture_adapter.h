#pragma once

#include "audio_chunker.h"
#include "audio_sink.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QObject>
#include <atomic>
#include <livekit/audio_source.h>
#include <memory>
#include <functional>
#include <optional>

namespace pcm::video {

// Backend lifetime is independent of the published LiveKit source. Keeping this
// small boundary injectable permits deterministic device-error/lifecycle tests.
class AudioCaptureSource {
public:
  virtual ~AudioCaptureSource() = default;
  virtual QIODevice *start() = 0;
  virtual void stop() = 0;
};

// Captures microphone audio via Qt Multimedia and feeds fixed-size PCM
// frames into a livekit::AudioSource. Runs entirely on the GUI thread: the
// AudioSource is constructed in real-time (non-buffered) mode specifically
// so captureFrame() never blocks — no worker thread is needed here, unlike
// VideoCaptureAdapter.
class AudioCaptureAdapter final : public QObject {
  Q_OBJECT
public:
  static constexpr int kSampleRate = 48000;
  static constexpr int kChannels = 1;
  static constexpr int kFrameMs = 10;

  explicit AudioCaptureAdapter(QObject *parent = nullptr);
  using SourceFactory = std::function<std::unique_ptr<AudioCaptureSource>(const QAudioDevice &)>;
  AudioCaptureAdapter(SourceFactory factory, QObject *parent = nullptr);
  ~AudioCaptureAdapter() override;

  [[nodiscard]] std::shared_ptr<livekit::AudioSource> audioSource() const { return mAudioSource; }
  [[nodiscard]] int framesCaptured() const { return mFramesCaptured.load(); }

  // Taps the raw mic samples (before chunking). Disabled by the provider while muted.
  [[nodiscard]] AudioTap &tap() { return mTap; }
  [[nodiscard]] std::optional<QAudioDevice> activeDevice() const { return mActiveDevice; }

  bool start(const QAudioDevice &device);
  void stop();

signals:
  void frameCaptured();
  void captureFailed(QString reason);
  void captureInterrupted();
  void captureResumed();

private slots:
  void onReadyRead();

private:
  // Real-time capture mode: the 3rd AudioSource constructor argument is a
  // queue_size_ms buffered-queue window, NOT a frame duration. It must stay
  // 0 — passing kFrameMs here puts AudioSource into buffered mode, where
  // captureFrame() can block the calling (GUI) thread for up to 20ms and
  // throw. mChunker's frame sizing below is unrelated to this argument.
  std::shared_ptr<livekit::AudioSource> mAudioSource{
      std::make_shared<livekit::AudioSource>(kSampleRate, kChannels, 0)};
  bool open(const QAudioDevice &device);
  SourceFactory mSourceFactory;
  std::unique_ptr<AudioCaptureSource> mSource;
  std::optional<QAudioDevice> mActiveDevice;
  QIODevice *mIoDevice{nullptr};
  AudioChunker mChunker{kSampleRate * kFrameMs / 1000, kChannels};
  std::atomic<int> mFramesCaptured{0};
  AudioTap mTap;
  uint64_t mCaptureGeneration{0};
};

} // namespace pcm::video
