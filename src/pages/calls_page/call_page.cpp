#include "call_page.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

CallPage::CallPage(pcm::video::DeviceManager *deviceManager, QWidget *parent) : QWidget(parent) {
  auto *outer = new QVBoxLayout(this);
  mStack = new QStackedWidget(this);
  outer->addWidget(mStack);

  buildDeviceCheckScreen(deviceManager);

  mConnectingScreen = new QWidget(this);
  new QVBoxLayout(mConnectingScreen);
  static_cast<QVBoxLayout *>(mConnectingScreen->layout())
      ->addWidget(new QLabel(tr("Connecting..."), mConnectingScreen));
  mStack->addWidget(mConnectingScreen);

  buildConnectedScreen();

  mReconnectingBanner = new QLabel(tr("Reconnecting..."), this);
  mReconnectingBanner->setObjectName("reconnectingBanner");
  mReconnectingBanner->setVisible(false);
  outer->addWidget(mReconnectingBanner);

  mEndedScreen = new QWidget(this);
  new QVBoxLayout(mEndedScreen);
  static_cast<QVBoxLayout *>(mEndedScreen->layout())
      ->addWidget(new QLabel(tr("Call ended."), mEndedScreen));
  mStack->addWidget(mEndedScreen);

  mStack->setCurrentWidget(mDeviceCheck);
}

void CallPage::buildDeviceCheckScreen(pcm::video::DeviceManager *deviceManager) {
  mDeviceCheck = new DeviceCheckWidget(deviceManager, this);
  mDeviceCheck->setObjectName("deviceCheckWidget");
  // DeviceCheckWidget::joinRequested is intentionally left unconnected here.
  // Task 12 wired a placeholder mSession->join(QString(), QString()) call in
  // its place, but that was always dead/wrong: it fired before a session
  // even existed to attach (attachSession() only ever gets called by
  // CallsPage, see below), and its empty url/token would never have worked
  // anyway. As of Task 13, CallsPage owns the whole join trigger: the entry
  // screen's own "Connect"/"Join" buttons fetch a token from
  // TokenBackendClient, then CallsPage constructs the VideoSession, calls
  // attachSession() on this CallPage, and immediately calls
  // session->join(url, token) itself. DeviceCheckWidget's own signal has no
  // remaining caller-visible purpose from CallPage's perspective — the
  // device-check screen still lets the user preview/pick camera and mic, but
  // the actual join is triggered one level up, before this screen is even
  // shown. Leaving the signal connection out makes that a documented no-op
  // rather than a silent dead branch.
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

  auto *videoRow = new QHBoxLayout();
  videoRow->addWidget(new pcm::video::RemoteVideoRenderer(mConnectedView), 1);
  mSidePanelHost = new QWidget(mConnectedView);
  mSidePanelHost->setVisible(false);
  new QVBoxLayout(mSidePanelHost);
  videoRow->addWidget(mSidePanelHost);
  layout->addLayout(videoRow);

  auto *controls = new QHBoxLayout();
  auto *leaveButton = new QPushButton(tr("Leave"), mConnectedView);
  leaveButton->setObjectName("leaveButton");
  connect(leaveButton, &QPushButton::clicked, this, &CallPage::leaveRequested);
  controls->addWidget(leaveButton);
  layout->addLayout(controls);

  mStack->addWidget(mConnectedView);
}

void CallPage::attachSession(pcm::video::VideoSession *session) {
  mSession = session;
  connect(session, &pcm::video::VideoSession::stateChanged, this, &CallPage::onSessionStateChanged);
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
  case VideoSessionState::WaitingForClient:
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
    mStack->setCurrentWidget(mEndedScreen);
    emit callEnded();
    break;
  }
}
