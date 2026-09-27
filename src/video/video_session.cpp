#include "video_session.h"

#include <QMetaObject>

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
  connected->addTransition(mProvider, &VideoProvider::remoteParticipantDisconnected, waitingForClient);
  connect(mProvider, &VideoProvider::joinFailed, this,
          [this](const QString &reason) { emit joinFailed(reason); });

  // reconnecting() is the SDK's own "actively retrying" signal — the only
  // thing that should drive the Reconnecting state. connectionLost() is
  // terminal (the SDK has given up, whether or not it ever reconnected
  // first) and always goes straight to Failed, from Connected,
  // WaitingForClient, or Reconnecting.
  //
  // Reconnecting is only reachable from Connected, not WaitingForClient:
  // Reconnecting's own exit (reconnected() -> Connected) has nowhere else
  // to go, so a WaitingForClient -> Reconnecting leg would report Connected
  // once the SDK reconnects even though no remote participant was ever
  // actually present — misreporting the call as live with nobody on it.
  // A network blip while still WaitingForClient is not otherwise
  // observable yet (no UI consumes this state today, #80's job); if that
  // needs its own visible state later, it needs a way back to
  // WaitingForClient specifically (e.g. tracking presence across the
  // reconnect), not just this single shared Reconnecting state.
  connected->addTransition(mProvider, &VideoProvider::reconnecting, reconnecting);
  reconnecting->addTransition(mProvider, &VideoProvider::reconnected, connected);
  reconnecting->addTransition(mProvider, &VideoProvider::connectionLost, failed);
  reconnecting->addTransition(&mReconnectTimer, &QTimer::timeout, failed);
  waitingForClient->addTransition(mProvider, &VideoProvider::connectionLost, failed);
  connected->addTransition(mProvider, &VideoProvider::connectionLost, failed);
  connect(reconnecting, &QState::entered, this, [this]() { mReconnectTimer.start(); });
  connect(connected, &QState::entered, this, [this]() { mReconnectTimer.stop(); });
  connect(&mReconnectTimer, &QTimer::timeout, this,
          [this]() { emit reconnectFailed(QStringLiteral("Reconnection timed out.")); });

  joining->addTransition(this, &VideoSession::requestLeave, leaving);
  waitingForClient->addTransition(this, &VideoSession::requestLeave, leaving);
  connected->addTransition(this, &VideoSession::requestLeave, leaving);
  reconnecting->addTransition(this, &VideoSession::requestLeave, leaving);

  connect(leaving, &QState::entered, this, [this]() {
    mReconnectTimer.stop();
    mProvider->leave();
  });
  leaving->addTransition(mProvider, &VideoProvider::left, ended);

  // Failed is a terminal state for the state machine, but not for the
  // provider: whatever local devices/room connection are still open (e.g.
  // a connectionLost() that arrived straight from Connected, never via
  // Reconnecting) must still be torn down on entry. No further
  // requestLeave transition is needed out of Failed — teardown already
  // happened here, so a caller's later leave() call has nothing left to do
  // and is safely ignored rather than needing a second path to Ended.
  connect(failed, &QState::entered, this, [this]() {
    mReconnectTimer.stop();
    mPendingUrl.clear();
    mPendingToken.clear();
    mProvider->leave();
  });

  mMachine.setInitialState(noMeeting);
  mMachine.start();
}

void VideoSession::join(const QString &url, const QString &token) {
  // QStateMachine::start() only *schedules* entry into the initial state
  // (it posts an internal event rather than entering synchronously), so a
  // requestJoin() emitted synchronously here could arrive before the
  // machine is listening for it. Posting this call, rather than emitting
  // requestJoin() directly, defers it onto this object's own event queue —
  // behind the machine's own already-queued start event, since both share
  // the same queue and are delivered in posting order. This makes join()
  // safe to call the instant the constructor returns, with no busy-wait.
  QMetaObject::invokeMethod(this, [this, url, token]() { emit requestJoin(url, token); },
                             Qt::QueuedConnection);
}

void VideoSession::leave() {
  QMetaObject::invokeMethod(this, [this]() { emit requestLeave(); }, Qt::QueuedConnection);
}

} // namespace pcm::video
