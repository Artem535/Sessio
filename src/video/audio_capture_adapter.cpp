#include "audio_capture_adapter.h"

#include <QAudioFormat>
#include <cstring>

namespace pcm::video {

AudioCaptureAdapter::AudioCaptureAdapter(QObject *parent) : QObject(parent) {}

AudioCaptureAdapter::~AudioCaptureAdapter() {
  stop();
}

void AudioCaptureAdapter::start(const QAudioDevice &device) {
  stop();
  mChunker.reset();

  QAudioFormat format;
  format.setSampleRate(kSampleRate);
  format.setChannelCount(kChannels);
  format.setSampleFormat(QAudioFormat::Int16);

  mSource = std::make_unique<QAudioSource>(device, format, nullptr);
  mIoDevice = mSource->start();
  if (mIoDevice) {
    connect(mIoDevice, &QIODevice::readyRead, this, &AudioCaptureAdapter::onReadyRead);
  } else {
    emit captureFailed(QStringLiteral("Failed to open audio device."));
  }
}

void AudioCaptureAdapter::stop() {
  if (mSource) {
    mSource->stop();
    mSource.reset();
  }
  mIoDevice = nullptr;
}

void AudioCaptureAdapter::onReadyRead() {
  if (!mIoDevice) {
    return;
  }

  const QByteArray bytes = mIoDevice->readAll();
  std::vector<int16_t> samples(static_cast<std::size_t>(bytes.size()) / sizeof(int16_t));
  std::memcpy(samples.data(), bytes.constData(), samples.size() * sizeof(int16_t));

  mTap.push(samples.data(), samples.size(), kSampleRate);

  for (const auto &pcmFrame : mChunker.push(samples)) {
    try {
      auto liveKitFrame = livekit::AudioFrame::create(
          kSampleRate, kChannels, pcmFrame.size() / static_cast<std::size_t>(kChannels));
      std::memcpy(liveKitFrame.data().data(), pcmFrame.data(), pcmFrame.size() * sizeof(int16_t));
      mAudioSource->captureFrame(liveKitFrame);
      mFramesCaptured.fetch_add(1);
      emit frameCaptured();
    } catch (const std::exception &e) {
      emit captureFailed(QString::fromUtf8(e.what()));
    }
  }
}

} // namespace pcm::video
