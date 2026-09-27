#pragma once

// Throwaway spike for issue #77 — not production quality.

#include <QAudioDevice>
#include <QAudioSink>
#include <QIODevice>
#include <QObject>

#include <atomic>
#include <memory>
#include <thread>

#include "livekit/livekit.h"

// Plays a subscribed remote audio track through a QAudioSink. Runs its own
// reader thread blocking on livekit::AudioStream::read() (pull-based, same
// as RemoteVideoRenderer). The reader thread only reads frames and extracts
// PCM + format metadata; it never touches the QAudioSink/QIODevice directly
// (QAudioSink's QIODevice interface is documented for use from the thread
// that owns the sink, i.e. the GUI thread here) — every write, and the
// QAudioSink's construction itself, is marshaled onto the GUI thread via
// QMetaObject::invokeMethod(..., Qt::QueuedConnection). The sink is created
// lazily, from the first frame's actual sampleRate()/numChannels(), since
// QAudioSink does not resample and a hardcoded guess would misplay a track
// that negotiates a different format.
class RemoteAudioPlayer final : public QObject {
  Q_OBJECT

public:
  explicit RemoteAudioPlayer(QObject *parent = nullptr);
  ~RemoteAudioPlayer() override;

  void attachTrack(const std::shared_ptr<livekit::Track> &track, const QAudioDevice &outputDevice);
  void detach();

private:
  std::shared_ptr<livekit::AudioStream> mStream;
  QAudioDevice mOutputDevice;
  std::unique_ptr<QAudioSink> mSink;
  QIODevice *mSinkDevice{nullptr};
  std::thread mReaderThread;
  std::atomic<bool> mRunning{false};

  void readerLoop();
  // Runs on the GUI thread only (invoked via queued connection from
  // readerLoop). Lazily creates mSink from the first frame's real format,
  // then writes this frame's PCM bytes.
  void deliverAudioOnGuiThread(QByteArray pcmBytes, int sampleRate, int numChannels);
};
