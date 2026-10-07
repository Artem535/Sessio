#include "remote_audio_player.h"

#include <QAudioFormat>
#include <QMetaObject>

namespace pcm::video {

RemoteAudioPlayer::RemoteAudioPlayer(QObject *parent) : QObject(parent) {}

RemoteAudioPlayer::~RemoteAudioPlayer() {
  detach();
}

void RemoteAudioPlayer::attachTrack(const std::shared_ptr<livekit::Track> &track,
                                    const QAudioDevice &outputDevice) {
  detach();

  if (!track) {
    return;
  }

  livekit::AudioStream::Options options;
  options.capacity = 2;
  mStream = livekit::AudioStream::fromTrack(track, options);
  if (!mStream) {
    return;
  }

  mOutputDevice = outputDevice;
  mRunning.store(true);
  mReaderThread = std::thread(&RemoteAudioPlayer::readerLoop, this, mGeneration);
}

void RemoteAudioPlayer::detach() {
  ++mGeneration;
  mRunning.store(false);
  if (mStream) {
    mStream->close();
  }
  if (mReaderThread.joinable()) {
    mReaderThread.join();
  }
  mSink.reset();
  mSinkDevice = nullptr;
  mStream.reset();
}

void RemoteAudioPlayer::readerLoop(uint64_t generation) {
  livekit::AudioFrameEvent event;
  while (mRunning.load()) {
    if (!mStream->read(event)) {
      break;
    }

    const auto &frame = event.frame;
    // AudioFrame::data() returns a std::vector<int16_t>& (not a raw
    // pointer/size pair) — see livekit/audio_frame.h. Task 5's
    // AudioCaptureAdapter hit the same mismatch against the brief's assumed
    // frame.data()/frame.size() shape; follow the same fix here.
    const auto &samples = frame.data();
    QByteArray bytes(reinterpret_cast<const char *>(samples.data()),
                     static_cast<qsizetype>(samples.size() * sizeof(int16_t)));
    const int sampleRate = frame.sampleRate();
    const int numChannels = frame.numChannels();

    if (numChannels == 1) {
      mTap.push(samples.data(), samples.size(), sampleRate);
    } else if (numChannels > 1) {
      const auto channels = static_cast<std::size_t>(numChannels);
      std::vector<int16_t> mono(samples.size() / channels);
      for (std::size_t i = 0; i < mono.size(); ++i) {
        int sum = 0;
        for (std::size_t c = 0; c < channels; ++c)
          sum += samples[i * channels + c];
        mono[i] = static_cast<int16_t>(sum / numChannels);
      }
      mTap.push(mono.data(), mono.size(), sampleRate);
    }

    QMetaObject::invokeMethod(
        this,
        [this, bytes = std::move(bytes), sampleRate, numChannels, generation]() mutable {
          deliverAudioOnGuiThread(std::move(bytes), sampleRate, numChannels, generation);
        },
        Qt::QueuedConnection);
  }
}

void RemoteAudioPlayer::deliverAudioOnGuiThread(QByteArray pcmBytes, const int sampleRate,
                                                const int numChannels, uint64_t generation) {
  if (!mRunning.load() || generation != mGeneration) {
    return;
  }

  if (!mSink) {
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(numChannels);
    format.setSampleFormat(QAudioFormat::Int16);

    mSink = std::make_unique<QAudioSink>(mOutputDevice, format, this);
    mSinkDevice = mSink->start();
  }

  if (mSinkDevice && mSinkDevice->isWritable()) {
    mSinkDevice->write(pcmBytes);
  }
}

} // namespace pcm::video
