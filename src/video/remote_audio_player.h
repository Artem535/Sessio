#pragma once

#include <QAudioDevice>
#include <QAudioSink>
#include <QObject>
#include <atomic>
#include <livekit/audio_stream.h>
#include <memory>
#include <thread>

namespace pcm::video {

// Plays a subscribed remote LiveKit audio track. Pulls frames on a
// dedicated std::thread (blocking AudioStream::read()) and marshals decoded
// PCM to the GUI thread, since QAudioSink's push-mode QIODevice interface
// must only be touched from the thread that owns the sink.
class RemoteAudioPlayer final : public QObject {
  Q_OBJECT
public:
  explicit RemoteAudioPlayer(QObject *parent = nullptr);
  ~RemoteAudioPlayer() override;

  void attachTrack(const std::shared_ptr<livekit::Track> &track, const QAudioDevice &outputDevice);
  void detach();

signals:
  void playbackFailed(QString reason);

private:
  friend struct RemoteAudioPlayerTestAccess;
  std::shared_ptr<livekit::AudioStream> mStream;
  QAudioDevice mOutputDevice;
  std::unique_ptr<QAudioSink> mSink;
  QIODevice *mSinkDevice{nullptr};
  std::thread mReaderThread;
  std::atomic<bool> mRunning{false};
  uint64_t mGeneration{0}; // owner-thread attachment epoch

  void readerLoop(uint64_t generation);
  void deliverAudioOnGuiThread(QByteArray pcmBytes, int sampleRate, int numChannels,
                               uint64_t generation);
};

} // namespace pcm::video
