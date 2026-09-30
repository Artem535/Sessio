#include "call_page.h"
#include "fake_video_provider.h"
#include "device_manager.h"
#include "busy_spinner.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLayout>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QToolButton>
#include <gtest/gtest.h>

using pcm::video::test::FakeVideoProvider;
using pcm::video::VideoSession;
using pcm::video::VideoSessionState;

namespace {

// VideoSession::join()/leave() post their internal requestJoin()/
// requestLeave() signals via Qt::QueuedConnection (see video_session.cpp),
// and its QStateMachine settles asynchronously. Firing all of a scenario's
// FakeVideoProvider::simulate*() calls back-to-back right after join()/
// leave(), then pumping once, races the state machine: a provider signal
// emitted before the session has actually reached the state that listens
// for it is silently dropped, and the session gets stuck. Waiting for each
// intermediate state — the same pattern as VideoSessionTest::waitForState in
// video_session_tests.cpp — is what makes this deterministic.
void waitForState(VideoSession &session, QSignalSpy &stateSpy, VideoSessionState target) {
  while (session.state() != target) {
    ASSERT_TRUE(stateSpy.wait(1000)) << "timed out waiting for state " << static_cast<int>(target);
  }
}

// QWidget::resize() alone does not synchronously invoke resizeEvent() unless
// the widget is actually visible (Qt defers the event until the widget's
// ancestor chain is shown). These tests never show() the page, so exercising
// VideoStage::layoutChildren() against a real, non-default size requires
// explicitly delivering the QResizeEvent after resizing.
void resizeAndDeliverEvent(QWidget *widget, const QSize &size) {
  const QSize oldSize = widget->size();
  widget->resize(size);
  QResizeEvent event(widget->size(), oldSize);
  QApplication::sendEvent(widget, &event);
}

} // namespace

TEST(CallPageTest, StartsOnDeviceCheckScreen) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  EXPECT_NE(page.findChild<QWidget *>("deviceCheckWidget"), nullptr);
  EXPECT_EQ(page.findChild<QWidget *>("connectedView"), nullptr);
}

TEST(CallPageTest, ConnectedStateShowsConnectedViewWithLeaveButton) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);

  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  provider->simulateJoined();
  waitForState(session, stateSpy, VideoSessionState::WaitingForClient);
  provider->simulateRemoteParticipantConnected();
  waitForState(session, stateSpy, VideoSessionState::Connected);

  EXPECT_NE(page.findChild<QWidget *>("connectedView"), nullptr);
  auto *leaveButton = page.findChild<QPushButton *>("leaveButton");
  ASSERT_NE(leaveButton, nullptr);

  QSignalSpy leaveSpy(&page, &CallPage::leaveRequested);
  leaveButton->click();
  EXPECT_EQ(leaveSpy.count(), 1);
}

TEST(CallPageTest, EndedStateEmitsCallEnded) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);
  QSignalSpy endedSpy(&page, &CallPage::callEnded);

  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  provider->simulateJoined();
  waitForState(session, stateSpy, VideoSessionState::WaitingForClient);
  session.leave();
  waitForState(session, stateSpy, VideoSessionState::Leaving);
  provider->simulateLeft();
  waitForState(session, stateSpy, VideoSessionState::Ended);

  EXPECT_EQ(endedSpy.count(), 1);
}

// Fixwave group 1, bug 3: a join failure used to route to the ended screen
// with only a generic "Call ended." label — the reason was dropped.
TEST(CallPageTest, JoinFailureReasonIsShownOnEndedScreen) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);

  auto *reasonLabel = page.findChild<QLabel *>("endedReasonLabel");
  ASSERT_NE(reasonLabel, nullptr);
  EXPECT_TRUE(reasonLabel->isHidden());

  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  provider->simulateJoinFailed("no route to host");
  waitForState(session, stateSpy, VideoSessionState::Failed);

  EXPECT_FALSE(reasonLabel->isHidden());
  EXPECT_EQ(reasonLabel->text(), QStringLiteral("no route to host"));
}

TEST(CallPageTest, ReconnectFailureReasonIsShownOnEndedScreen) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider, std::chrono::milliseconds(50));
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);

  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  provider->simulateJoined();
  waitForState(session, stateSpy, VideoSessionState::WaitingForClient);
  provider->simulateRemoteParticipantConnected();
  waitForState(session, stateSpy, VideoSessionState::Connected);
  provider->simulateReconnecting();
  waitForState(session, stateSpy, VideoSessionState::Reconnecting);
  waitForState(session, stateSpy, VideoSessionState::Failed);

  auto *reasonLabel = page.findChild<QLabel *>("endedReasonLabel");
  ASSERT_NE(reasonLabel, nullptr);
  EXPECT_FALSE(reasonLabel->isHidden());
  EXPECT_EQ(reasonLabel->text(), QStringLiteral("Reconnection timed out."));
}

TEST(CallPageTest, NormalLeaveAfterEarlierFailureShowsNoStaleReason) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);

  // First session fails with a reason...
  auto *failingProvider = new FakeVideoProvider();
  VideoSession failingSession(failingProvider);
  page.attachSession(&failingSession);
  QSignalSpy failingSpy(&failingSession, &VideoSession::stateChanged);
  failingSession.join("wss://x", "token");
  waitForState(failingSession, failingSpy, VideoSessionState::Joining);
  failingProvider->simulateJoinFailed("no route to host");
  waitForState(failingSession, failingSpy, VideoSessionState::Failed);
  auto *reasonLabel = page.findChild<QLabel *>("endedReasonLabel");
  ASSERT_NE(reasonLabel, nullptr);
  ASSERT_FALSE(reasonLabel->isHidden());

  // ...then a fresh session is attached and left normally: no reason.
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);
  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  provider->simulateJoined();
  waitForState(session, stateSpy, VideoSessionState::WaitingForClient);
  session.leave();
  waitForState(session, stateSpy, VideoSessionState::Leaving);
  provider->simulateLeft();
  waitForState(session, stateSpy, VideoSessionState::Ended);

  EXPECT_TRUE(reasonLabel->isHidden());
  EXPECT_TRUE(reasonLabel->text().isEmpty());
}

// Fixwave group 1, bug 2: CallPage used to show only its own blank
// renderer, while the provider attached the real remote track to a
// different, never-embedded widget.
TEST(CallPageTest, EmbedsProviderRemoteVideoWidgetInPlaceOfPlaceholder) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *placeholder = page.findChild<QWidget *>("remoteVideoPlaceholder");
  ASSERT_NE(placeholder, nullptr);
  EXPECT_TRUE(placeholder->isVisibleTo(placeholder->parentWidget()));

  auto *provider = new FakeVideoProvider();
  QPointer<QLabel> remoteVideo = new QLabel("remote video"); // unparented, like LiveKit's
  provider->mRemoteVideoWidget = remoteVideo;
  VideoSession session(provider);
  page.attachSession(&session);

  auto *videoStage = page.findChild<QWidget *>("videoStage");
  ASSERT_NE(videoStage, nullptr);
  // Widgets default to Qt's un-laid-out geometry (0,0,100,30) until
  // actually resized; give VideoStage a real size so layoutChildren()'s
  // setGeometry(rect()) call is the thing actually being verified below,
  // not two widgets that both happen to still be at the same default.
  resizeAndDeliverEvent(videoStage, QSize(800, 600));

  EXPECT_TRUE(page.isAncestorOf(remoteVideo));
  EXPECT_EQ(remoteVideo->parentWidget(), videoStage);
  EXPECT_TRUE(remoteVideo->isVisibleTo(videoStage));
  EXPECT_FALSE(placeholder->isVisibleTo(videoStage));
  // It takes the placeholder's slot: VideoStage stretches exactly one
  // remote widget to fill it at a time.
  EXPECT_EQ(remoteVideo->geometry(), videoStage->rect());

  // The widget is only borrowed: CallPage never deletes it. (Here the test
  // plays the provider's role and releases it.)
  delete remoteVideo.data();
}

TEST(CallPageTest, KeepsPlaceholderWhenProviderHasNoRemoteVideoWidget) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider(); // remoteVideoWidget() == nullptr
  VideoSession session(provider);
  page.attachSession(&session);

  auto *placeholder = page.findChild<QWidget *>("remoteVideoPlaceholder");
  auto *videoStage = page.findChild<QWidget *>("videoStage");
  ASSERT_NE(placeholder, nullptr);
  ASSERT_NE(videoStage, nullptr);
  // See EmbedsProviderRemoteVideoWidgetInPlaceOfPlaceholder for why this
  // resize is needed before the geometry assertion below means anything.
  resizeAndDeliverEvent(videoStage, QSize(800, 600));
  EXPECT_TRUE(placeholder->isVisibleTo(videoStage));
  EXPECT_EQ(placeholder->parentWidget(), videoStage);
  EXPECT_EQ(placeholder->geometry(), videoStage->rect());
}

TEST(CallPageTest, ReattachingHandsBorrowedRemoteVideoWidgetBackToItsProvider) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *placeholder = page.findChild<QWidget *>("remoteVideoPlaceholder");
  auto *videoStage = page.findChild<QWidget *>("videoStage");
  ASSERT_NE(videoStage, nullptr);

  auto *firstProvider = new FakeVideoProvider();
  QPointer<QLabel> firstVideo = new QLabel("first");
  firstProvider->mRemoteVideoWidget = firstVideo;
  VideoSession firstSession(firstProvider);
  page.attachSession(&firstSession);
  ASSERT_TRUE(page.isAncestorOf(firstVideo));

  auto *secondProvider = new FakeVideoProvider();
  VideoSession secondSession(secondProvider);
  page.attachSession(&secondSession);
  ASSERT_FALSE(firstVideo.isNull());
  EXPECT_EQ(firstVideo->parent(), nullptr);
  EXPECT_TRUE(placeholder->isVisibleTo(videoStage));
  EXPECT_EQ(placeholder->parentWidget(), videoStage);
  delete firstVideo.data();
}

TEST(CallPageTest, ToleratesEmbeddedRemoteVideoWidgetDestroyedWithItsProvider) {
  // Mirrors CallsPage::startJoin(): the old session (and with it the old
  // provider and its remote-video widget) is destroyed while that widget is
  // still embedded, and only then is the new session attached.
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *placeholder = page.findChild<QWidget *>("remoteVideoPlaceholder");
  auto *videoStage = page.findChild<QWidget *>("videoStage");
  ASSERT_NE(videoStage, nullptr);
  // See EmbedsProviderRemoteVideoWidgetInPlaceOfPlaceholder for why this
  // resize is needed before the geometry assertion below means anything.
  resizeAndDeliverEvent(videoStage, QSize(800, 600));

  auto *oldProvider = new FakeVideoProvider();
  QPointer<QLabel> oldVideo = new QLabel("old");
  oldProvider->mRemoteVideoWidget = oldVideo;
  auto oldSession = std::make_unique<VideoSession>(oldProvider);
  page.attachSession(oldSession.get());
  ASSERT_TRUE(page.isAncestorOf(oldVideo));

  // What LiveKitVideoProvider's destructor does to its embedded renderer.
  delete oldVideo.data();
  oldSession.reset();

  auto *newProvider = new FakeVideoProvider();
  QPointer<QLabel> newVideo = new QLabel("new");
  newProvider->mRemoteVideoWidget = newVideo;
  VideoSession newSession(newProvider);
  page.attachSession(&newSession);
  EXPECT_EQ(newVideo->parentWidget(), videoStage);
  EXPECT_EQ(newVideo->geometry(), videoStage->rect());
  EXPECT_FALSE(placeholder->isVisibleTo(videoStage));
  delete newVideo.data();
}

// Fixwave group 5, bug 1: a live call dropping (VideoProvider::connectionLost())
// used to bounce the user straight to the ended screen with zero explanation
// — the reason string was dropped on the floor. Reuses the exact same
// onSessionFailureReason()/mEndedReasonLabel mechanism as joinFailed()/
// reconnectFailed(), proven by the two tests above.
TEST(CallPageTest, ConnectionLostReasonIsShownOnEndedScreen) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);

  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  provider->simulateJoined();
  waitForState(session, stateSpy, VideoSessionState::WaitingForClient);
  provider->simulateRemoteParticipantConnected();
  waitForState(session, stateSpy, VideoSessionState::Connected);

  provider->simulateConnectionLost("room ended");
  waitForState(session, stateSpy, VideoSessionState::Failed);

  auto *reasonLabel = page.findChild<QLabel *>("endedReasonLabel");
  ASSERT_NE(reasonLabel, nullptr);
  EXPECT_FALSE(reasonLabel->isHidden());
  EXPECT_EQ(reasonLabel->text(), QStringLiteral("room ended"));
}

// Fixwave group 5, bug 1: mediaError() (a local camera/mic/publish problem)
// used to have no UI surface at all — a broken camera/mic failed silently.
// This proves the new banner shows the reason WITHOUT switching away from
// the connected screen and WITHOUT touching the ended-screen mechanism,
// since the call itself keeps going.
TEST(CallPageTest, MediaErrorShowsBannerWithoutEndingCallOrTouchingEndedReason) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);

  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  provider->simulateJoined();
  waitForState(session, stateSpy, VideoSessionState::WaitingForClient);
  provider->simulateRemoteParticipantConnected();
  waitForState(session, stateSpy, VideoSessionState::Connected);

  auto *banner = page.findChild<QLabel *>("mediaErrorBanner");
  ASSERT_NE(banner, nullptr);
  EXPECT_TRUE(banner->isHidden());

  provider->simulateMediaError("camera unplugged");

  // Observable immediately, before any auto-hide timer could possibly fire.
  EXPECT_FALSE(banner->isHidden());
  EXPECT_EQ(banner->text(), QStringLiteral("camera unplugged"));
  EXPECT_EQ(session.state(), VideoSessionState::Connected);
  auto *reasonLabel = page.findChild<QLabel *>("endedReasonLabel");
  ASSERT_NE(reasonLabel, nullptr);
  EXPECT_TRUE(reasonLabel->isHidden());
  EXPECT_TRUE(reasonLabel->text().isEmpty());
}

// Fixwave userfeedback group A, bug 2: VideoSessionState::Joining and
// VideoSessionState::WaitingForClient both used to route to mConnectingScreen
// with the exact same static "Connecting..." text, leaving a user who had
// already joined and was waiting on the other participant indistinguishable
// from someone still connecting.
TEST(CallPageTest, WaitingForClientShowsDistinctMessageFromJoining) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);

  auto *connectingLabel = page.findChild<QLabel *>("connectingLabel");
  ASSERT_NE(connectingLabel, nullptr);

  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  EXPECT_EQ(connectingLabel->text(), QStringLiteral("Connecting..."));

  provider->simulateJoined();
  waitForState(session, stateSpy, VideoSessionState::WaitingForClient);
  EXPECT_EQ(connectingLabel->text(), QStringLiteral("Waiting for the other participant to join..."));
}

TEST(CallPageTest, SidePanelToggleHiddenByDefaultUntilMadeVisible) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  EXPECT_EQ(page.findChild<QPushButton *>("notesToggleButton"), nullptr);

  page.setSidePanelToggleVisible(true);
  EXPECT_NE(page.findChild<QPushButton *>("notesToggleButton"), nullptr);
}

TEST(CallPageTest, ConnectingScreenShowsCenteredSpinnerAndHeadline) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);

  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);

  auto *connectingLabel = page.findChild<QLabel *>("connectingLabel");
  ASSERT_NE(connectingLabel, nullptr);
  EXPECT_EQ(connectingLabel->alignment() & Qt::AlignHCenter, Qt::AlignHCenter);

  auto *spinner = page.findChild<pcm::widgets::BusySpinner *>("connectingSpinner");
  ASSERT_NE(spinner, nullptr);
  EXPECT_TRUE(spinner->isVisibleTo(spinner->parentWidget()));
}

TEST(CallPageTest, ReconnectingBannerHasASpinner) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *spinner = page.findChild<pcm::widgets::BusySpinner *>("reconnectingSpinner");
  ASSERT_NE(spinner, nullptr);
}

TEST(CallPageTest, ShowsLocalPreviewWhenProviderSuppliesOne) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  QPointer<QLabel> localPreview = new QLabel("local");
  provider->mLocalVideoWidget = localPreview;
  VideoSession session(provider);
  page.attachSession(&session);

  EXPECT_TRUE(page.isAncestorOf(localPreview));
  EXPECT_TRUE(localPreview->isVisibleTo(localPreview->parentWidget()));
  delete localPreview.data();
}

TEST(CallPageTest, NoLocalPreviewWidgetWhenProviderHasNone) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider(); // localVideoWidget() == nullptr
  VideoSession session(provider);
  page.attachSession(&session);

  auto *videoStage = page.findChild<QWidget *>("videoStage");
  ASSERT_NE(videoStage, nullptr);
  // No crash and no orphaned preview child beyond the remote-video slot.
  EXPECT_EQ(videoStage->findChildren<QLabel *>().size(), 0);
}

TEST(CallPageTest, SwappingProviderHandsLocalPreviewBackUnparented) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);

  auto *firstProvider = new FakeVideoProvider();
  QPointer<QLabel> firstPreview = new QLabel("first-local");
  firstProvider->mLocalVideoWidget = firstPreview;
  VideoSession firstSession(firstProvider);
  page.attachSession(&firstSession);
  ASSERT_TRUE(page.isAncestorOf(firstPreview));

  auto *secondProvider = new FakeVideoProvider(); // no local preview
  VideoSession secondSession(secondProvider);
  page.attachSession(&secondSession);

  ASSERT_FALSE(firstPreview.isNull());
  EXPECT_EQ(firstPreview->parent(), nullptr);
  delete firstPreview.data();
}

TEST(CallPageTest, MicrophoneToggleButtonCallsProviderAndStartsEnabled) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);

  auto *micButton = page.findChild<QToolButton *>("microphoneToggleButton");
  ASSERT_NE(micButton, nullptr);
  EXPECT_TRUE(micButton->isChecked());

  micButton->setChecked(false);
  EXPECT_EQ(provider->mSetMicrophoneEnabledCallCount, 1);
  EXPECT_FALSE(provider->isMicrophoneEnabled());
}

TEST(CallPageTest, CameraToggleButtonCallsProvider) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);

  auto *cameraButton = page.findChild<QToolButton *>("cameraToggleButton");
  ASSERT_NE(cameraButton, nullptr);
  EXPECT_TRUE(cameraButton->isChecked());

  cameraButton->setChecked(false);
  EXPECT_EQ(provider->mSetCameraEnabledCallCount, 1);
  EXPECT_FALSE(provider->isCameraEnabled());
}

TEST(CallPageTest, FullscreenToggleButtonExistsAndIsCheckable) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *fullscreenButton = page.findChild<QToolButton *>("fullscreenToggleButton");
  ASSERT_NE(fullscreenButton, nullptr);
  EXPECT_TRUE(fullscreenButton->isCheckable());
  EXPECT_FALSE(fullscreenButton->isChecked());
}

TEST(CallPageTest, DevicesButtonOpensPopoverWithThreeCombos) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *devicesButton = page.findChild<QToolButton *>("devicesButton");
  ASSERT_NE(devicesButton, nullptr);

  devicesButton->click();

  auto *cameraCombo = page.findChild<QComboBox *>("deviceCameraCombo");
  auto *microphoneCombo = page.findChild<QComboBox *>("deviceMicrophoneCombo");
  auto *speakerCombo = page.findChild<QComboBox *>("deviceSpeakerCombo");
  EXPECT_NE(cameraCombo, nullptr);
  EXPECT_NE(microphoneCombo, nullptr);
  EXPECT_NE(speakerCombo, nullptr);
}

TEST(CallPageTest, SelectingADeviceCallsSwitchOnTheAttachedProvider) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);

  auto *devicesButton = page.findChild<QToolButton *>("devicesButton");
  ASSERT_NE(devicesButton, nullptr);
  devicesButton->click();

  auto *microphoneCombo = page.findChild<QComboBox *>("deviceMicrophoneCombo");
  ASSERT_NE(microphoneCombo, nullptr);
  if (microphoneCombo->count() > 1) {
    microphoneCombo->setCurrentIndex(microphoneCombo->currentIndex() == 0 ? 1 : 0);
    EXPECT_EQ(provider->mSwitchMicrophoneCallCount, 1);
  }
}

TEST(CallPageTest, SetSidePanelExpandedByDefaultChecksTheNotesToggle) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  page.setSidePanelToggleVisible(true);
  auto *toggle = page.findChild<QPushButton *>("notesToggleButton");
  ASSERT_NE(toggle, nullptr);
  EXPECT_FALSE(toggle->isChecked());

  page.setSidePanelExpandedByDefault(true);
  EXPECT_TRUE(toggle->isChecked());
}

TEST(CallPageTest, SetSidePanelExpandedByDefaultIsANoOpWithoutATotoggleYet) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  // No crash when called before setSidePanelToggleVisible(true) (client
  // mode, or before Application's eventKnownForCurrentCall handler ever
  // fires).
  page.setSidePanelExpandedByDefault(true);
  EXPECT_EQ(page.findChild<QPushButton *>("notesToggleButton"), nullptr);
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
