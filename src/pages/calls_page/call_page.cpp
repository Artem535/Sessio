#include "call_page.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
// How long CallPage's transient media-error banner stays visible before
// auto-hiding. A device error (camera/mic/publish failure) is a one-off
// notice, not a persistent condition the UI needs to keep nagging about —
// but long enough for a user who glances away for a moment to still catch
// it. Chosen within the 5-8s range suggested for this kind of transient,
// non-fatal notice; not user-configurable since there's no evidence a fixed
// value here needs to be.
constexpr int kMediaErrorBannerAutoHideMs = 6000;
} // namespace

CallPage::CallPage(pcm::video::DeviceManager *deviceManager, QWidget *parent) : QWidget(parent) {
  auto *outer = new QVBoxLayout(this);
  mStack = new QStackedWidget(this);
  outer->addWidget(mStack);

  buildDeviceCheckScreen(deviceManager);

  mConnectingScreen = new QWidget(this);
  new QVBoxLayout(mConnectingScreen);
  mConnectingLabel = new QLabel(tr("Connecting..."), mConnectingScreen);
  mConnectingLabel->setObjectName("connectingLabel");
  static_cast<QVBoxLayout *>(mConnectingScreen->layout())->addWidget(mConnectingLabel);
  mStack->addWidget(mConnectingScreen);

  buildConnectedScreen();

  mReconnectingBanner = new QLabel(tr("Reconnecting..."), this);
  mReconnectingBanner->setObjectName("reconnectingBanner");
  mReconnectingBanner->setVisible(false);
  outer->addWidget(mReconnectingBanner);

  mEndedScreen = new QWidget(this);
  auto *endedLayout = new QVBoxLayout(mEndedScreen);
  endedLayout->addWidget(new QLabel(tr("Call ended."), mEndedScreen));
  // Shown only when the session failed with a reason (joinFailed()/
  // reconnectFailed()); a normal user-initiated leave shows nothing here.
  mEndedReasonLabel = new QLabel(mEndedScreen);
  mEndedReasonLabel->setObjectName("endedReasonLabel");
  mEndedReasonLabel->setWordWrap(true);
  mEndedReasonLabel->setVisible(false);
  endedLayout->addWidget(mEndedReasonLabel);
  mStack->addWidget(mEndedScreen);

  mStack->setCurrentWidget(mDeviceCheck);
}

void CallPage::buildDeviceCheckScreen(pcm::video::DeviceManager *deviceManager) {
  mDeviceCheck = new DeviceCheckWidget(deviceManager, this);
  mDeviceCheck->setObjectName("deviceCheckWidget");
  // Fix round 1: DeviceCheckWidget::joinRequested is the real join gate, per
  // the design spec (PrejoinCheck is a real device-review step with its own
  // Join button, not a screen the flow blows through automatically). Task 12
  // originally wired this to a placeholder mSession->join(QString(),
  // QString()) call, which was always dead/wrong (fired before a session
  // even existed to attach, with empty url/token). Task 13's first pass
  // instead had CallsPage call session->join(url, token) immediately once a
  // token arrived, bypassing this button entirely — which contradicted both
  // the design spec and Task 12's own note that "Task 13 replaces this
  // [placeholder] with the real flow" at the point where this button is
  // clicked. This relay is the fix: CallPage has no token client and no
  // url/token of its own, so it cannot call join() itself — it just forwards
  // "the user confirmed" to whoever attached the session (CallsPage), which
  // does hold the pending url/token and performs the actual join() call.
  connect(mDeviceCheck, &DeviceCheckWidget::joinRequested, this, &CallPage::joinConfirmed);
  mStack->addWidget(mDeviceCheck);
}

void CallPage::buildConnectedScreen() {
  // The connected screen's structure (leave button, side-panel host) is
  // built eagerly here — setSidePanelToggleVisible() must work immediately
  // after construction, before any session ever attaches (see
  // CallPageTest.SidePanelToggleHiddenByDefaultUntilMadeVisible) — but its
  // "connectedView" object name is deliberately NOT assigned yet.
  // CallPageTest.StartsOnDeviceCheckScreen requires findChild("connectedView")
  // to return nullptr before any call has actually connected, so the name is
  // assigned lazily in onSessionStateChanged() the first time this screen is
  // actually shown.
  mConnectedView = new QWidget(this);
  auto *layout = new QVBoxLayout(mConnectedView);

  // Slot 0 of mVideoRow holds the remote video. It starts out as a blank
  // CallPage-owned placeholder; updateRemoteVideoWidget() swaps in the
  // attached provider's real remoteVideoWidget() (the one the provider
  // actually attaches the subscribed remote track to) when it has one.
  mVideoRow = new QHBoxLayout();
  mRemoteVideoPlaceholder = new pcm::video::RemoteVideoRenderer(mConnectedView);
  mRemoteVideoPlaceholder->setObjectName("remoteVideoPlaceholder");
  mVideoRow->addWidget(mRemoteVideoPlaceholder, 1);
  mActiveRemoteVideoWidget = mRemoteVideoPlaceholder;
  mSidePanelHost = new QWidget(mConnectedView);
  mSidePanelHost->setVisible(false);
  new QVBoxLayout(mSidePanelHost);
  mVideoRow->addWidget(mSidePanelHost);
  layout->addLayout(mVideoRow);

  auto *controls = new QHBoxLayout();
  auto *leaveButton = new QPushButton(tr("Leave"), mConnectedView);
  leaveButton->setObjectName("leaveButton");
  connect(leaveButton, &QPushButton::clicked, this, &CallPage::leaveRequested);
  controls->addWidget(leaveButton);
  layout->addLayout(controls);

  // Non-fatal, transient local-device notice — see mMediaErrorBanner's doc
  // comment in call_page.h. Hidden by default; onMediaError() shows it and
  // sets its text, independent of mReconnectingBanner and the ended screen.
  // Added last (after `controls`) so setSidePanelToggleVisible()'s hard-coded
  // mConnectedView->layout()->itemAt(1) lookup — which means "the controls
  // row" — keeps working unchanged.
  mMediaErrorBanner = new QLabel(mConnectedView);
  mMediaErrorBanner->setObjectName("mediaErrorBanner");
  mMediaErrorBanner->setWordWrap(true);
  mMediaErrorBanner->setVisible(false);
  layout->addWidget(mMediaErrorBanner);

  mStack->addWidget(mConnectedView);
}

void CallPage::attachSession(pcm::video::VideoSession *session) {
  // A previously attached session that is still alive must stop driving
  // this page (CallsPage normally destroys it first, which disconnects it
  // anyway — this covers any caller that doesn't).
  if (mSession) {
    disconnect(mSession.data(), nullptr, this, nullptr);
  }
  mSession = session;
  mLastFailureReason.clear();
  refreshEndedReason();
  connect(session, &pcm::video::VideoSession::stateChanged, this, &CallPage::onSessionStateChanged);
  connect(session, &pcm::video::VideoSession::joinFailed, this, &CallPage::onSessionFailureReason);
  connect(session, &pcm::video::VideoSession::reconnectFailed, this,
          &CallPage::onSessionFailureReason);
  // connectionLost() is terminal too (the state machine has already moved to
  // Failed by the time a UI observer sees this) — it reuses the exact same
  // ended-screen mechanism as joinFailed()/reconnectFailed().
  connect(session, &pcm::video::VideoSession::connectionLost, this,
          &CallPage::onSessionFailureReason);
  // mediaError() never ends the call — its own, separate, non-fatal banner.
  connect(session, &pcm::video::VideoSession::mediaError, this, &CallPage::onMediaError);
  updateRemoteVideoWidget();
}

void CallPage::updateRemoteVideoWidget() {
  QWidget *provided = nullptr;
  if (mSession && mSession->provider()) {
    provided = mSession->provider()->remoteVideoWidget();
  }
  QWidget *target = provided ? provided : mRemoteVideoPlaceholder;
  if (target == mActiveRemoteVideoWidget) {
    return;
  }

  // mActiveRemoteVideoWidget may already be null here: a borrowed provider
  // widget is destroyed together with its provider (e.g. CallsPage::
  // startJoin() replacing its session before attaching the new one), and
  // Qt has then already removed it from mVideoRow.
  if (QWidget *previous = mActiveRemoteVideoWidget.data()) {
    mVideoRow->removeWidget(previous);
    previous->hide();
    if (previous != mRemoteVideoPlaceholder) {
      // Borrowed from a provider: hand it back instead of deleting it —
      // the provider still owns its lifetime.
      previous->setParent(nullptr);
    }
  }

  if (target->parentWidget() != mConnectedView) {
    target->setParent(mConnectedView);
  }
  mVideoRow->insertWidget(0, target, /*stretch=*/1);
  // setParent() hides a widget, and a swapped-out placeholder was hidden
  // explicitly above; either way it has to be shown again.
  target->show();
  mActiveRemoteVideoWidget = target;
}

void CallPage::onSessionFailureReason(const QString &reason) {
  mLastFailureReason = reason;
  // VideoSession emits joinFailed()/reconnectFailed() before entering
  // Failed today, but don't depend on that ordering: if the reason arrives
  // after the ended screen is already up, show it there immediately.
  if (mStack->currentWidget() == mEndedScreen) {
    refreshEndedReason();
  }
}

void CallPage::onMediaError(const QString &reason) {
  // Testable synchronously: the banner's text/visibility are set here,
  // immediately, before any auto-hide timer fires — a test can assert on
  // them right after emitting mediaError() with no need to wait.
  mMediaErrorBanner->setText(reason);
  mMediaErrorBanner->setVisible(true);
  QTimer::singleShot(kMediaErrorBannerAutoHideMs, this, [this, reason]() {
    // Only auto-hide if this is still the most recent reason shown — a
    // fresh mediaError() (or another call to this slot) in the meantime
    // already reset the timer's effect by overwriting the text, so an
    // earlier timer firing later must not blank a newer message.
    if (mMediaErrorBanner->text() == reason) {
      mMediaErrorBanner->setVisible(false);
    }
  });
}

void CallPage::refreshEndedReason() {
  mEndedReasonLabel->setText(mLastFailureReason);
  mEndedReasonLabel->setVisible(!mLastFailureReason.isEmpty());
}

void CallPage::setSidePanelWidget(QWidget *panel) {
  if (mSidePanel) {
    mSidePanelHost->layout()->removeWidget(mSidePanel);
    mSidePanel->setParent(nullptr);
  }
  mSidePanel = panel;
  if (panel) {
    mSidePanelHost->layout()->addWidget(panel);
  }
}

void CallPage::setSidePanelToggleVisible(bool visible) {
  if (visible && !mNotesToggleButton) {
    mNotesToggleButton = new QPushButton(tr("Notes"), mConnectedView);
    mNotesToggleButton->setObjectName("notesToggleButton");
    mNotesToggleButton->setCheckable(true);
    connect(mNotesToggleButton, &QPushButton::toggled, this,
            [this](bool checked) { mSidePanelHost->setVisible(checked); });
    static_cast<QHBoxLayout *>(mConnectedView->layout()->itemAt(1)->layout())
        ->addWidget(mNotesToggleButton);
  } else if (!visible && mNotesToggleButton) {
    delete mNotesToggleButton;
    mNotesToggleButton = nullptr;
  }
}

void CallPage::onSessionStateChanged(const pcm::video::VideoSessionState state) {
  using pcm::video::VideoSessionState;
  mReconnectingBanner->setVisible(state == VideoSessionState::Reconnecting);

  switch (state) {
  case VideoSessionState::NoMeeting:
  case VideoSessionState::Provisioned:
  case VideoSessionState::PrejoinCheck:
    mStack->setCurrentWidget(mDeviceCheck);
    break;
  case VideoSessionState::Joining:
    mConnectingLabel->setText(tr("Connecting..."));
    mStack->setCurrentWidget(mConnectingScreen);
    break;
  case VideoSessionState::WaitingForClient:
    mConnectingLabel->setText(tr("Waiting for the other participant to join..."));
    mStack->setCurrentWidget(mConnectingScreen);
    break;
  case VideoSessionState::Connected:
  case VideoSessionState::Reconnecting:
    mConnectedView->setObjectName("connectedView");
    mStack->setCurrentWidget(mConnectedView);
    break;
  case VideoSessionState::Leaving:
    break;
  case VideoSessionState::Ended:
  case VideoSessionState::Failed:
    refreshEndedReason();
    mStack->setCurrentWidget(mEndedScreen);
    emit callEnded();
    break;
  }
}
