// Throwaway spike for issue #77 — not production quality.
#include "audio_capture_adapter.h"

#include <cstdint>
#include <cstring>
#include <iostream>

AudioCaptureAdapter::AudioCaptureAdapter(QObject *parent)
    : QObject(parent),
      // Third ctor arg is queue_size_ms (AudioSource's internal buffer
      // window), NOT a frame duration — 0 means real-time capture mode
      // (captureFrame() is consumed synchronously, no blocking/timeout).
      // kFrameMs is unrelated and only sizes AudioChunker's frames below.
      mAudioSource(std::make_shared<livekit::AudioSource>(kSampleRate, kChannels, 0)),
      mChunker(static_cast<std::size_t>(kSampleRate * kFrameMs / 1000), kChannels) {}

AudioCaptureAdapter::~AudioCaptureAdapter() { stop(); }

void AudioCaptureAdapter::start(const QAudioDevice &device) {
  stop();
  // Discard any partial-frame remainder left over from the previous device
  // so it isn't prepended to the new device's audio stream.
  mChunker.reset();

  QAudioFormat format;
  format.setSampleRate(kSampleRate);
  format.setChannelCount(kChannels);
  format.setSampleFormat(QAudioFormat::Int16);

  mSource = std::make_unique<QAudioSource>(device, format, nullptr);
  mIoDevice = mSource->start();
  if (!mIoDevice) {
    std::cerr << "[audio capture] QAudioSource::start() returned null (error="
              << static_cast<int>(mSource->error()) << ")" << std::endl;
    return;
  }
  connect(mIoDevice, &QIODevice::readyRead, this, &AudioCaptureAdapter::onReadyRead);
}

void AudioCaptureAdapter::stop() {
  if (mSource) {
    mSource->stop();
    mIoDevice = nullptr;
    mSource.reset();
  }
}

void AudioCaptureAdapter::onReadyRead() {
  if (!mIoDevice) {
    return;
  }
  const QByteArray bytes = mIoDevice->readAll();
  if (bytes.isEmpty()) {
    return;
  }

  std::vector<int16_t> samples(static_cast<std::size_t>(bytes.size()) / sizeof(int16_t));
  std::memcpy(samples.data(), bytes.constData(), samples.size() * sizeof(int16_t));

  for (const auto &pcmFrame : mChunker.push(samples)) {
    auto liveKitFrame = livekit::AudioFrame::create(kSampleRate, kChannels, pcmFrame.size() / kChannels);
    auto &dst = liveKitFrame.data();
    std::copy(pcmFrame.begin(), pcmFrame.end(), dst.begin());

    try {
      mAudioSource->captureFrame(liveKitFrame);
      mFramesCaptured.fetch_add(1);
      emit frameCaptured();
    } catch (const std::exception &e) {
      std::cerr << "[audio capture] captureFrame failed: " << e.what() << std::endl;
    }
  }
}
