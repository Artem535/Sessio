#pragma once

#include <QAudioDevice>
#include <QAudioSource>
#include <QIODevice>
#include <QObject>

#include <atomic>
#include <memory>

#include "audio_chunker.h"
#include "livekit/livekit.h"

// Drives a QAudioSource through Qt Multimedia and pushes fixed-size PCM
// frames into a livekit::AudioSource. Owns no LiveKit track — callers
// publish audioSource() themselves (see spike_window.cpp).
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

private slots:
  void onReadyRead();

private:
  std::shared_ptr<livekit::AudioSource> mAudioSource;
  std::unique_ptr<QAudioSource> mSource;
  QIODevice *mIoDevice{nullptr};
  pcm::spike::AudioChunker mChunker;
  std::atomic<int> mFramesCaptured{0};
};
