#pragma once

#include "video_provider.h"
#include "video_session_state.h"

#include <QState>
#include <QStateMachine>
#include <QTimer>
#include <chrono>

namespace pcm::video {

class VideoSession final : public QObject {
  Q_OBJECT
public:
  explicit VideoSession(VideoProvider *provider,
                        std::chrono::milliseconds reconnectTimeout = std::chrono::seconds(30),
                        QObject *parent = nullptr);

  [[nodiscard]] VideoSessionState state() const { return mState; }
  // The provider this session drives (Qt-owned by this session).
  [[nodiscard]] VideoProvider *provider() const { return mProvider; }
  [[nodiscard]] ParticipantModel *participants() const { return mProvider->participants(); }

  void join(const QString &url, const QString &token);
  void leave();

signals:
  void stateChanged(pcm::video::VideoSessionState state);
  void joinFailed(QString reason);
  void reconnectFailed(QString reason);
  // Relays VideoProvider::connectionLost()'s reason string. The state
  // machine already transitions to Failed off the provider's own signal
  // (see the addTransition(mProvider, &VideoProvider::connectionLost, ...)
  // calls in the constructor) — this signal exists purely so a UI observer
  // (CallPage) can learn WHY, the same way joinFailed()/reconnectFailed()
  // already do for their own terminal transitions.
  void connectionLost(QString reason);
  // Relays VideoProvider::mediaError()'s reason string. Unlike
  // connectionLost(), this never drives the state machine (a local
  // camera/microphone/publish problem does not end the call) — it exists
  // solely so a UI observer can show a non-fatal, transient notice.
  void mediaError(QString reason);
  // Internal drive signals for the QStateMachine's transitions — not part
  // of the class's conceptual public contract (join()/leave() are), but
  // Qt requires signals to be either public or protected, never private.
  // QState::addTransition binds to an object's signal, so join()/leave()
  // emit these instead of touching mMachine directly.
  void requestJoin(QString url, QString token);
  void requestLeave();

private:
  VideoProvider *mProvider;
  QString mPendingUrl;
  QString mPendingToken;
  VideoSessionState mState{VideoSessionState::NoMeeting};
  QTimer mReconnectTimer;
  QStateMachine mMachine;
};

} // namespace pcm::video

Q_DECLARE_METATYPE(pcm::video::VideoSessionState)
