#include "video_session.h"

#include <QCoreApplication>

namespace pcm::video {

VideoSession::VideoSession(VideoProvider *provider, const std::chrono::milliseconds reconnectTimeout,
                           QObject *parent)
    : QObject(parent), mProvider(provider) {
  mProvider->setParent(this);

  mReconnectTimer.setSingleShot(true);
  mReconnectTimer.setInterval(static_cast<int>(reconnectTimeout.count()));

  auto *noMeeting = new QState(&mMachine);
  auto *provisioned = new QState(&mMachine);
  auto *prejoinCheck = new QState(&mMachine);
  auto *joining = new QState(&mMachine);
  auto *waitingForClient = new QState(&mMachine);
  auto *connected = new QState(&mMachine);
  auto *reconnecting = new QState(&mMachine);
  auto *leaving = new QState(&mMachine);
  auto *ended = new QState(&mMachine);
  auto *failed = new QState(&mMachine);

  const auto wireEntered = [this](QState *state, const VideoSessionState value) {
    connect(state, &QState::entered, this, [this, value]() {
      mState = value;
      emit stateChanged(mState);
    });
  };
  wireEntered(noMeeting, VideoSessionState::NoMeeting);
  wireEntered(provisioned, VideoSessionState::Provisioned);
  wireEntered(prejoinCheck, VideoSessionState::PrejoinCheck);
  wireEntered(joining, VideoSessionState::Joining);
  wireEntered(waitingForClient, VideoSessionState::WaitingForClient);
  wireEntered(connected, VideoSessionState::Connected);
  wireEntered(reconnecting, VideoSessionState::Reconnecting);
  wireEntered(leaving, VideoSessionState::Leaving);
  wireEntered(ended, VideoSessionState::Ended);
  wireEntered(failed, VideoSessionState::Failed);

  // NoMeeting -> Provisioned -> PrejoinCheck -> Joining: the middle two
  // legs are unconditional (fire immediately on entry, no external event),
  // since neither has an observable gate in this task — see this task's
  // note above the header.
  noMeeting->addTransition(this, &VideoSession::requestJoin, provisioned);
  provisioned->addTransition(prejoinCheck);
  prejoinCheck->addTransition(joining);

  connect(this, &VideoSession::requestJoin, this, [this](const QString &url, const QString &token) {
    mPendingUrl = url;
    mPendingToken = token;
  });
  connect(joining, &QState::entered, this,
          [this]() { mProvider->join(mPendingUrl, mPendingToken); });

  joining->addTransition(mProvider, &VideoProvider::joined, waitingForClient);
  joining->addTransition(mProvider, &VideoProvider::joinFailed, failed);
  waitingForClient->addTransition(mProvider, &VideoProvider::remoteParticipantConnected, connected);
  connect(mProvider, &VideoProvider::joinFailed, this,
          [this](const QString &reason) { emit joinFailed(reason); });

  connected->addTransition(mProvider, &VideoProvider::connectionLost, reconnecting);
  reconnecting->addTransition(mProvider, &VideoProvider::reconnected, connected);
  reconnecting->addTransition(&mReconnectTimer, &QTimer::timeout, failed);
  connect(reconnecting, &QState::entered, this, [this]() { mReconnectTimer.start(); });
  connect(connected, &QState::entered, this, [this]() { mReconnectTimer.stop(); });
  connect(&mReconnectTimer, &QTimer::timeout, this,
          [this]() { emit reconnectFailed(QStringLiteral("Reconnection timed out.")); });

  joining->addTransition(this, &VideoSession::requestLeave, leaving);
  waitingForClient->addTransition(this, &VideoSession::requestLeave, leaving);
  connected->addTransition(this, &VideoSession::requestLeave, leaving);
  reconnecting->addTransition(this, &VideoSession::requestLeave, leaving);

  connect(leaving, &QState::entered, this, [this]() { mProvider->leave(); });
  leaving->addTransition(mProvider, &VideoProvider::left, ended);

  mMachine.setInitialState(noMeeting);
  mMachine.start();
  // QStateMachine::start() only *schedules* entry into the initial state and
  // registration of its signal transitions (it posts an internal event
  // rather than doing this synchronously). Without pumping the event loop
  // here, a caller that calls join()/leave() immediately after constructing
  // a VideoSession — which is a perfectly reasonable thing to do, and what
  // this class's own tests do — would emit requestJoin()/requestLeave()
  // before the machine is listening for it, silently dropping the
  // transition and leaving the session stuck in NoMeeting forever. Block
  // here until the machine actually reports itself running so join()/
  // leave() are safe to call the instant this constructor returns.
  while (!mMachine.isRunning()) {
    QCoreApplication::processEvents();
  }
}

void VideoSession::join(const QString &url, const QString &token) {
  emit requestJoin(url, token);
}

void VideoSession::leave() {
  emit requestLeave();
}

} // namespace pcm::video
