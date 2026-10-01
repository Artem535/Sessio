#include "video_session.h"

#include <QMetaObject>
#include <QSignalTransition>

namespace pcm::video {

namespace {
class PresenceTransition final : public QSignalTransition {
public:
  PresenceTransition(QObject *sender, const char *signal, ParticipantModel *model,
                     bool present, QState *source, QState *target)
      : QSignalTransition(sender, signal, source), mModel(model), mPresent(present) {
    setTargetState(target);
  }
protected:
  bool eventTest(QEvent *event) override {
    return QSignalTransition::eventTest(event) && (mModel->remoteCount() > 0) == mPresent;
  }
private:
  ParticipantModel *mModel;
  bool mPresent;
};
} // namespace

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
  wireEntered(waitingForClient, VideoSessionState::WaitingForParticipants);
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

  new PresenceTransition(mProvider, SIGNAL(joined()), participants(), false, joining, waitingForClient);
  new PresenceTransition(mProvider, SIGNAL(joined()), participants(), true, joining, connected);
  joining->addTransition(mProvider, &VideoProvider::joinFailed, failed);
  new PresenceTransition(participants(), SIGNAL(remoteCountChanged(int)), participants(), true,
                         waitingForClient, connected);
  new PresenceTransition(participants(), SIGNAL(remoteCountChanged(int)), participants(), false,
                         connected, waitingForClient);
  connect(mProvider, &VideoProvider::joinFailed, this,
          [this](const QString &reason) { emit joinFailed(reason); });

  // Pure relays: the state machine's own connectionLost() transitions are
  // wired separately below (unchanged from before this relay was added),
  // and mediaError() never touches the state machine at all — see both
  // signals' doc comments in video_session.h.
  connect(mProvider, &VideoProvider::connectionLost, this,
          [this](const QString &reason) { emit connectionLost(reason); });
  connect(mProvider, &VideoProvider::mediaError, this,
          [this](const QString &reason) { emit mediaError(reason); });

  // reconnecting() is the SDK's own "actively retrying" signal — the only
  // thing that should drive the Reconnecting state. connectionLost() is
  // terminal (the SDK has given up, whether or not it ever reconnected
  // first) and always goes straight to Failed, from Connected,
  // WaitingForParticipants, or Reconnecting.
  //
  // Resolve reconnect from current presence, including updates during retries.
  connected->addTransition(mProvider, &VideoProvider::reconnecting, reconnecting);
  waitingForClient->addTransition(mProvider, &VideoProvider::reconnecting, reconnecting);
  new PresenceTransition(mProvider, SIGNAL(reconnected()), participants(), true, reconnecting, connected);
  new PresenceTransition(mProvider, SIGNAL(reconnected()), participants(), false, reconnecting, waitingForClient);
  reconnecting->addTransition(mProvider, &VideoProvider::connectionLost, failed);
  reconnecting->addTransition(&mReconnectTimer, &QTimer::timeout, failed);
  waitingForClient->addTransition(mProvider, &VideoProvider::connectionLost, failed);
  connected->addTransition(mProvider, &VideoProvider::connectionLost, failed);
  connect(reconnecting, &QState::entered, this, [this]() { mReconnectTimer.start(); });
  connect(connected, &QState::entered, this, [this]() { mReconnectTimer.stop(); });
  connect(waitingForClient, &QState::entered, this, [this]() { mReconnectTimer.stop(); });
  connect(&mReconnectTimer, &QTimer::timeout, this,
          [this]() { emit reconnectFailed(QStringLiteral("Reconnection timed out.")); });

  joining->addTransition(this, &VideoSession::requestLeave, leaving);
  waitingForClient->addTransition(this, &VideoSession::requestLeave, leaving);
  connected->addTransition(this, &VideoSession::requestLeave, leaving);
  reconnecting->addTransition(this, &VideoSession::requestLeave, leaving);

  connect(leaving, &QState::entered, this, [this]() {
    mReconnectTimer.stop();
    mProvider->leave();
    participants()->clear();
    mPendingUrl.clear();
    mPendingToken.clear();
  });
  leaving->addTransition(mProvider, &VideoProvider::left, ended);
  connect(ended, &QState::entered, participants(), &ParticipantModel::clear);

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
    participants()->clear();
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
