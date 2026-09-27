#pragma once

#include "audio_chunker.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QObject>
#include <atomic>
#include <livekit/audio_source.h>
#include <memory>

namespace pcm::video {

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
  ~AudioCaptureAdapter() override;

  [[nodiscard]] std::shared_ptr<livekit::AudioSource> audioSource() const { return mAudioSource; }
  [[nodiscard]] int framesCaptured() const { return mFramesCaptured.load(); }

  void start(const QAudioDevice &device);
  void stop();

signals:
  void frameCaptured();
  void captureFailed(QString reason);

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
  std::unique_ptr<QAudioSource> mSource;
  QIODevice *mIoDevice{nullptr};
  AudioChunker mChunker{kSampleRate * kFrameMs / 1000, kChannels};
  std::atomic<int> mFramesCaptured{0};
};

} // namespace pcm::video
