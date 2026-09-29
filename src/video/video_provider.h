#pragma once

#include <QObject>
#include <QString>
#include <QAudioDevice>
#include <QCameraDevice>

class QWidget;

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
  using QObject::QObject;
  ~VideoProvider() override = default;

  // Connects to the given server url with the given (pre-obtained) JWT
  // token, and publishes local audio/video tracks. This provider does not
  // fetch the token itself — the caller obtains it via the MeetingProvider/
  // token-backend layer before calling join().
  virtual void join(const QString &url, const QString &token) = 0;

  // Disconnects and releases all local devices/tracks. Safe to call even if
  // never successfully joined.
  virtual void leave() = 0;

  // The widget this provider renders the remote participant's video into,
  // or nullptr if it has none (e.g. a test double). The provider owns the
  // widget's lifetime; a UI embedding it may reparent it into its own
  // layout, but must hand it back (setParent(nullptr)) rather than delete
  // it when swapping it out, and must tolerate it being destroyed along
  // with the provider (hold it through a QPointer).
  virtual QWidget *remoteVideoWidget() { return nullptr; }

  // The widget this provider renders the LOCAL camera preview into (the
  // same capture that is being published, not a second parallel camera
  // session), or nullptr if it has none (e.g. a test double). Same
  // ownership contract as remoteVideoWidget(): the provider owns it: a UI
  // embedding it may reparent it, but must hand it back
  // (setParent(nullptr)) rather than delete it when swapping it out.
  virtual QWidget *localVideoWidget() { return nullptr; }

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

signals:
  void joined();
  void joinFailed(QString reason);
  void left();
  void remoteParticipantConnected();
  void remoteParticipantDisconnected();
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
};

} // namespace pcm::video
