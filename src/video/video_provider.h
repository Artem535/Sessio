#pragma once

#include <QObject>
#include <QString>
#include <QAudioDevice>
#include <QCameraDevice>
#include <memory>
#include "audio_sink.h"
#include "participant_model.h"
#include "video_frame_source.h"

namespace pcm::video {

// Abstraction over the actual in-call media session: connecting to the
// video backend, publishing local tracks, subscribing to and rendering
// remote tracks. Unlike MeetingProvider (pcm::meeting), this is NOT
// polymorphic across multiple real backends — LiveKitVideoProvider is the
// only production implementation. The interface exists solely so
// VideoSession and the future native call UI (#80) can be tested against a
// FakeVideoProvider without a real SDK, network, camera, or microphone.
//
// join()/leave() are asynchronous: callers observe the outcome via signals,
// never a return value.
class VideoProvider : public QObject {
  Q_OBJECT
public:
  explicit VideoProvider(QObject *parent = nullptr)
      : QObject(parent), mParticipants(new ParticipantModel(this)) {}
  ~VideoProvider() override = default;
  [[nodiscard]] ParticipantModel *participants() const { return mParticipants; }
  [[nodiscard]] virtual VideoFrameSource *frameSource(const QString &id) {
    Q_UNUSED(id);
    return nullptr;
  }

  // Connects to the given server url with the given (pre-obtained) JWT
  // token, and publishes local audio/video tracks. This provider does not
  // fetch the token itself — the caller obtains it via the MeetingProvider/
  // token-backend layer before calling join().
  virtual void join(const QString &url, const QString &token) = 0;

  // Disconnects and releases all local devices/tracks. Safe to call even if
  // never successfully joined.
  virtual void leave() = 0;

  // Mutes/unmutes the corresponding locally published track. Never stops
  // physically capturing the device (matches the LiveKit SDK's own
  // documented mute() contract) — only whether the track is transmitted.
  virtual void setMicrophoneEnabled(bool enabled) { Q_UNUSED(enabled); }
  virtual void setCameraEnabled(bool enabled) { Q_UNUSED(enabled); }
  [[nodiscard]] virtual bool isMicrophoneEnabled() const { return true; }
  [[nodiscard]] virtual bool isCameraEnabled() const { return true; }

  // Switches the corresponding local capture device mid-call. Errors
  // (device removed, already in use) surface through the existing
  // mediaError() signal below — no new error signal.
  virtual void switchCamera(const QCameraDevice &device) { Q_UNUSED(device); }
  virtual void switchMicrophone(const QAudioDevice &device) { Q_UNUSED(device); }
  virtual void switchSpeaker(const QAudioDevice &device) { Q_UNUSED(device); }

  // Installs (or clears, with nullptr) the receiver of call audio for
  // transcription. Only set while a consented session exists.
  virtual void setAudioSink(std::shared_ptr<AudioSink> sink) { Q_UNUSED(sink); }
  [[nodiscard]] virtual qint64 callElapsedMs() const { return 0; }

signals:
  void joined();
  void joinFailed(QString reason);
  void left();
  void participantJoined(QString id);
  void participantLeft(QString id);
  void audioInterrupted(QString id);
  void audioResumed(QString id);
  void microphoneChanged(QAudioDevice device);
  void reconnecting();
  void reconnected();
  // Terminal: the SDK has given up on the connection (whether or not it
  // ever emitted reconnecting() first). Distinct from reconnecting(), which
  // is the SDK's own signal that it is actively retrying — VideoSession
  // treats connectionLost() as a reason to give up, not a reason to enter
  // its Reconnecting state.
  void connectionLost(QString reason);
  // A local capture problem (camera/microphone/publish failure) — never
  // network loss. Kept separate from connectionLost() so a device error
  // does not drive VideoSession's Connected/Reconnecting/Failed graph,
  // which models the state of the connection to the server, not of local
  // devices.
  void mediaError(QString reason);

private:
  ParticipantModel *mParticipants;
};

} // namespace pcm::video
