#include "call_page.h"
#include "fake_video_provider.h"
#include "device_manager.h"

#include <QApplication>
#include <QLabel>
#include <QLayout>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
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

  EXPECT_TRUE(page.isAncestorOf(remoteVideo));
  EXPECT_EQ(remoteVideo->parentWidget(), placeholder->parentWidget());
  EXPECT_TRUE(remoteVideo->isVisibleTo(placeholder->parentWidget()));
  EXPECT_FALSE(placeholder->isVisibleTo(placeholder->parentWidget()));
  // It takes the placeholder's slot: first in the video row, left of the
  // side-panel host.
  auto *videoRow = placeholder->parentWidget()->layout()->itemAt(0)->layout();
  ASSERT_NE(videoRow, nullptr);
  EXPECT_EQ(videoRow->indexOf(remoteVideo), 0);
  EXPECT_EQ(videoRow->indexOf(placeholder), -1);

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
  ASSERT_NE(placeholder, nullptr);
  EXPECT_TRUE(placeholder->isVisibleTo(placeholder->parentWidget()));
  auto *videoRow = placeholder->parentWidget()->layout()->itemAt(0)->layout();
  EXPECT_EQ(videoRow->indexOf(placeholder), 0);
}

TEST(CallPageTest, ReattachingHandsBorrowedRemoteVideoWidgetBackToItsProvider) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *placeholder = page.findChild<QWidget *>("remoteVideoPlaceholder");

  auto *firstProvider = new FakeVideoProvider();
  QPointer<QLabel> firstVideo = new QLabel("first");
  firstProvider->mRemoteVideoWidget = firstVideo;
  VideoSession firstSession(firstProvider);
  page.attachSession(&firstSession);
  ASSERT_TRUE(page.isAncestorOf(firstVideo));

  // A session whose provider has no widget: the placeholder comes back and
  // the first provider's widget is handed back unparented, not deleted.
  auto *secondProvider = new FakeVideoProvider();
  VideoSession secondSession(secondProvider);
  page.attachSession(&secondSession);
  ASSERT_FALSE(firstVideo.isNull());
  EXPECT_EQ(firstVideo->parent(), nullptr);
  EXPECT_TRUE(placeholder->isVisibleTo(placeholder->parentWidget()));
  auto *videoRow = placeholder->parentWidget()->layout()->itemAt(0)->layout();
  EXPECT_EQ(videoRow->indexOf(placeholder), 0);
  EXPECT_EQ(videoRow->indexOf(firstVideo), -1);
  delete firstVideo.data();
}

TEST(CallPageTest, ToleratesEmbeddedRemoteVideoWidgetDestroyedWithItsProvider) {
  // Mirrors CallsPage::startJoin(): the old session (and with it the old
  // provider and its remote-video widget) is destroyed while that widget is
  // still embedded, and only then is the new session attached.
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *placeholder = page.findChild<QWidget *>("remoteVideoPlaceholder");
  auto *videoRow = placeholder->parentWidget()->layout()->itemAt(0)->layout();
  const int rowCountWithOneVideo = videoRow->count();

  auto *oldProvider = new FakeVideoProvider();
  QPointer<QLabel> oldVideo = new QLabel("old");
  oldProvider->mRemoteVideoWidget = oldVideo;
  auto oldSession = std::make_unique<VideoSession>(oldProvider);
  page.attachSession(oldSession.get());
  ASSERT_TRUE(page.isAncestorOf(oldVideo));

  // What LiveKitVideoProvider's destructor does to its embedded renderer.
  delete oldVideo.data();
  oldSession.reset();
  EXPECT_EQ(videoRow->count(), rowCountWithOneVideo - 1) << "Qt drops a deleted widget from its layout";

  auto *newProvider = new FakeVideoProvider();
  QPointer<QLabel> newVideo = new QLabel("new");
  newProvider->mRemoteVideoWidget = newVideo;
  VideoSession newSession(newProvider);
  page.attachSession(&newSession);
  EXPECT_EQ(videoRow->indexOf(newVideo), 0);
  EXPECT_EQ(videoRow->count(), rowCountWithOneVideo);
  EXPECT_FALSE(placeholder->isVisibleTo(placeholder->parentWidget()));
  delete newVideo.data();
}

TEST(CallPageTest, SidePanelToggleHiddenByDefaultUntilMadeVisible) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  EXPECT_EQ(page.findChild<QPushButton *>("notesToggleButton"), nullptr);

  page.setSidePanelToggleVisible(true);
  EXPECT_NE(page.findChild<QPushButton *>("notesToggleButton"), nullptr);
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
