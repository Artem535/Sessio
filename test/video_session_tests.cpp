#include "video_session.h"

#include "fake_video_provider.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <gtest/gtest.h>

namespace {

class VideoSessionTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    static int argc = 0;
    static QCoreApplication app(argc, nullptr);
  }

  // Pumps the event loop (via stateSpy.wait()) until VideoSession reaches
  // `target`, or fails the test if 1s elapses without reaching it. Needed
  // because QStateMachine's Provisioned/PrejoinCheck/Joining cascade (and
  // every provider-driven transition) settles asynchronously.
  static void waitForState(pcm::video::VideoSession &session, QSignalSpy &stateSpy,
                           pcm::video::VideoSessionState target) {
    while (session.state() != target) {
      ASSERT_TRUE(stateSpy.wait(1000)) << "timed out waiting for state "
                                       << static_cast<int>(target);
    }
  }
};

} // namespace

TEST_F(VideoSessionTest, StartsInNoMeetingState) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  EXPECT_EQ(session.state(), pcm::video::VideoSessionState::NoMeeting);
}

TEST_F(VideoSessionTest, JoinReachesJoiningStateThroughProvisionedAndPrejoinCheck) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);

  EXPECT_EQ(fake->mJoinCallCount, 1);
  EXPECT_EQ(fake->mLastJoinUrl, QStringLiteral("wss://example.invalid"));
  EXPECT_EQ(fake->mLastJoinToken, QStringLiteral("token"));
}

TEST_F(VideoSessionTest, JoinedThenRemoteParticipantConnectedReachesConnected) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);

  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForClient);

  fake->simulateRemoteParticipantConnected();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);
}

TEST_F(VideoSessionTest, JoinFailureTransitionsToFailed) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);
  QSignalSpy failedSpy(&session, &pcm::video::VideoSession::joinFailed);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);

  fake->simulateJoinFailed("no route to host");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Failed);

  ASSERT_EQ(failedSpy.count(), 1);
  EXPECT_EQ(failedSpy.first().at(0).toString(), QStringLiteral("no route to host"));
}

TEST_F(VideoSessionTest, ConnectionLostEntersReconnectingThenReconnectedReturnsToConnected) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake, std::chrono::milliseconds(200));
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForClient);
  fake->simulateRemoteParticipantConnected();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);

  fake->simulateConnectionLost("network blip");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Reconnecting);

  fake->simulateReconnected();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);
}

TEST_F(VideoSessionTest, ReconnectTimeoutTransitionsToFailedWithoutReconnection) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake, std::chrono::milliseconds(50));
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForClient);
  fake->simulateRemoteParticipantConnected();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);

  QSignalSpy reconnectFailedSpy(&session, &pcm::video::VideoSession::reconnectFailed);
  fake->simulateConnectionLost("network blip");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Reconnecting);

  // No simulateReconnected() call — the 50ms timer must fire on its own.
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Failed);
  ASSERT_EQ(reconnectFailedSpy.count(), 1);
}

TEST_F(VideoSessionTest, LeaveFromConnectedTransitionsToEnded) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForClient);
  fake->simulateRemoteParticipantConnected();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);

  session.leave();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Leaving);
  EXPECT_EQ(fake->mLeaveCallCount, 1);

  fake->simulateLeft();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Ended);
}

TEST_F(VideoSessionTest, RepeatedJoinLeaveDestroyCyclesDoNotHang) {
  // Lifecycle-stress acceptance criterion (this plan's Task 9 section, and
  // the #77 spike's outstanding "clean leave/destruction" gate item) for
  // VideoSession's own state machine, independent of the real SDK's own
  // lifecycle (covered separately by Task 7's LiveKitVideoProvider smoke
  // test).
  for (int i = 0; i < 50; ++i) {
    auto *fake = new pcm::video::test::FakeVideoProvider();
    pcm::video::VideoSession session(fake, std::chrono::milliseconds(20));
    QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

    session.join("wss://example.invalid", "token");
    waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
    fake->simulateJoined();
    waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForClient);
    fake->simulateRemoteParticipantConnected();
    waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);

    session.leave();
    waitForState(session, stateSpy, pcm::video::VideoSessionState::Leaving);
    fake->simulateLeft();
    waitForState(session, stateSpy, pcm::video::VideoSessionState::Ended);
  }
}
