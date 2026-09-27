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

  void join(const QString &url, const QString &token);
  void leave();

signals:
  void stateChanged(pcm::video::VideoSessionState state);
  void joinFailed(QString reason);
  void reconnectFailed(QString reason);
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
