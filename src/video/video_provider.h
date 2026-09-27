#pragma once

#include <QObject>
#include <QString>

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

signals:
  void joined();
  void joinFailed(QString reason);
  void left();
  void remoteParticipantConnected();
  void reconnecting();
  void reconnected();
  void connectionLost(QString reason);
};

} // namespace pcm::video
