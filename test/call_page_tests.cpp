#include "call_page.h"
#include "fake_video_provider.h"
#include "device_manager.h"

#include <QApplication>
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
