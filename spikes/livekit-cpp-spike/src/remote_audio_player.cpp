// Throwaway spike for issue #77 — not production quality.
#include "remote_audio_player.h"

#include <QByteArray>
#include <QMetaObject>

#include <cstdint>
#include <iostream>
#include <utility>

RemoteAudioPlayer::RemoteAudioPlayer(QObject *parent) : QObject(parent) {}

RemoteAudioPlayer::~RemoteAudioPlayer() { detach(); }

void RemoteAudioPlayer::attachTrack(const std::shared_ptr<livekit::Track> &track,
                                    const QAudioDevice &outputDevice) {
  detach();
  if (!track) {
    return;
  }

  livekit::AudioStream::Options opts;
  mStream = livekit::AudioStream::fromTrack(track, opts);
  if (!mStream) {
    std::cerr << "[remote audio] AudioStream::fromTrack failed" << std::endl;
    return;
  }

  // QAudioSink is constructed lazily in deliverAudioOnGuiThread(), once the
  // first AudioFrameEvent tells us the track's real sample rate/channel
  // count. Just remember which output device to use.
  mOutputDevice = outputDevice;

  mRunning.store(true);
  mReaderThread = std::thread(&RemoteAudioPlayer::readerLoop, this);
}

void RemoteAudioPlayer::detach() {
  mRunning.store(false);
  if (mStream) {
    mStream->close();  // wakes a blocking read() so the reader thread can exit promptly
  }
  if (mReaderThread.joinable()) {
    mReaderThread.join();
  }
  // At this point mRunning is false and the reader thread is joined, so any
  // deliverAudioOnGuiThread() call still sitting in the GUI thread's queued-
  // event queue (posted before detach() ran) will see mRunning == false when
  // it eventually executes and will no-op instead of touching a freed sink.
  if (mSink) {
    mSink->stop();
    mSink.reset();
  }
  mSinkDevice = nullptr;
  mStream.reset();
}

void RemoteAudioPlayer::readerLoop() {
  while (mRunning.load()) {
    livekit::AudioFrameEvent afe;
    if (!mStream->read(afe)) {
      break;
    }
    const auto &pcm = afe.frame.data();
    if (pcm.empty()) {
      continue;
    }

    // Extract everything this frame needs before crossing threads: PCM
    // bytes, plus the actual negotiated sample rate/channel count (these are
    // per-track properties reported by the SDK, not a fixed constant —
    // QAudioSink does not resample, so the sink must be built to match).
    QByteArray bytes(reinterpret_cast<const char *>(pcm.data()),
                     static_cast<qsizetype>(pcm.size() * sizeof(int16_t)));
    const int sampleRate = afe.frame.sampleRate();
    const int numChannels = afe.frame.numChannels();

    // Never touch mSink/mSinkDevice from this reader thread. QAudioSink's
    // QIODevice push-mode interface is documented (Qt "Threading model and
    // buffering") as intended for use from the thread that owns the sink —
    // marshal onto the GUI thread, same pattern RemoteVideoRenderer uses for
    // frame delivery.
    QMetaObject::invokeMethod(
        this,
        [this, bytes = std::move(bytes), sampleRate, numChannels]() mutable {
          deliverAudioOnGuiThread(std::move(bytes), sampleRate, numChannels);
        },
        Qt::QueuedConnection);
  }
}

void RemoteAudioPlayer::deliverAudioOnGuiThread(QByteArray pcmBytes, int sampleRate, int numChannels) {
  if (!mRunning.load()) {
    return;  // detach() ran while this queued call was pending
  }

  if (!mSink) {
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(numChannels);
    format.setSampleFormat(QAudioFormat::Int16);

    mSink = std::make_unique<QAudioSink>(mOutputDevice, format, this);
    mSinkDevice = mSink->start();
    if (!mSinkDevice) {
      std::cerr << "[remote audio] QAudioSink::start() returned null" << std::endl;
      return;
    }
  }

  if (mSinkDevice) {
    mSinkDevice->write(pcmBytes);
  }
}
