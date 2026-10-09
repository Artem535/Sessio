#include "audio_capture_adapter.h"

#include <QAudioFormat>
#include <QPointer>
#include <QThread>
#include <cstring>
#include <utility>

namespace pcm::video {
namespace {
class QtAudioCaptureSource final : public AudioCaptureSource {
public:
  explicit QtAudioCaptureSource(const QAudioDevice &device)
      : mSource(device, format()) {}
  QIODevice *start() override { return mSource.start(); }
  void stop() override { mSource.stop(); }
private:
  static QAudioFormat format() {
    QAudioFormat value;
    value.setSampleRate(AudioCaptureAdapter::kSampleRate);
    value.setChannelCount(AudioCaptureAdapter::kChannels);
    value.setChannelConfig(
        QAudioFormat::defaultChannelConfigForChannelCount(AudioCaptureAdapter::kChannels));
    value.setSampleFormat(QAudioFormat::Int16);
    return value;
  }
  QAudioSource mSource;
};
}

AudioCaptureAdapter::AudioCaptureAdapter(QObject *parent)
    : AudioCaptureAdapter([](const QAudioDevice &device) {
        return std::make_unique<QtAudioCaptureSource>(device);
      }, parent) {}

AudioCaptureAdapter::AudioCaptureAdapter(SourceFactory factory, QObject *parent)
    : QObject(parent), mSourceFactory(std::move(factory)) {}

AudioCaptureAdapter::~AudioCaptureAdapter() {
  stop();
}

bool AudioCaptureAdapter::start(const QAudioDevice &device) {
  Q_ASSERT(QThread::currentThread() == thread());
  const QPointer<AudioCaptureAdapter> self(this);
  const auto previous = mActiveDevice;
  const auto generation = mCaptureGeneration + 1;
  stop();
  if (!self || generation != mCaptureGeneration) return false;
  if (open(device)) return true;
  if (!self || generation != mCaptureGeneration) return false;
  emit captureFailed(QStringLiteral("Failed to open audio device."));
  if (!self || generation != mCaptureGeneration) return false;
  stop();
  const auto recoveryGeneration = mCaptureGeneration;
  if (previous && open(*previous)) return true;
  if (!self || recoveryGeneration != mCaptureGeneration) return false;
  stop();
  return false;
}

bool AudioCaptureAdapter::open(const QAudioDevice &device) {
  const QPointer<AudioCaptureAdapter> self(this);
  mSource = mSourceFactory(device);
  if (!mSource) return false;
  mIoDevice = mSource->start();
  if (mIoDevice) {
    mActiveDevice = device;
    const auto generation = mCaptureGeneration;
    const QPointer<QIODevice> input = mIoDevice;
    connect(mIoDevice, &QIODevice::readyRead, this, [this, input, generation] {
      if (input && input == mIoDevice && generation == mCaptureGeneration) onReadyRead();
    });
    emit captureResumed();
    return self && generation == mCaptureGeneration && mIoDevice && mActiveDevice.has_value();
  } else {
    return false;
  }
}

void AudioCaptureAdapter::stop() {
  Q_ASSERT(QThread::currentThread() == thread());
  ++mCaptureGeneration;
  const bool capturing = mIoDevice != nullptr;
  if (mIoDevice) disconnect(mIoDevice, nullptr, this, nullptr);
  mIoDevice = nullptr;
  mActiveDevice.reset();
  mChunker.reset();
  auto source = std::exchange(mSource, nullptr);
  if (capturing) emit captureInterrupted();
  if (source) {
    source->stop();
  }
}

void AudioCaptureAdapter::onReadyRead() {
  if (!mIoDevice) {
    return;
  }

  const auto generation = mCaptureGeneration;
  const QPointer<AudioCaptureAdapter> self(this);
  const QByteArray bytes = mIoDevice->readAll();
  if (!self || generation != mCaptureGeneration) return;
  std::vector<int16_t> samples(static_cast<std::size_t>(bytes.size()) / sizeof(int16_t));
  std::memcpy(samples.data(), bytes.constData(), samples.size() * sizeof(int16_t));

  mTap.push(samples.data(), samples.size(), kSampleRate);
  if (!self || generation != mCaptureGeneration) return;

  for (const auto &pcmFrame : mChunker.push(samples)) {
    if (!self || generation != mCaptureGeneration) return;
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
