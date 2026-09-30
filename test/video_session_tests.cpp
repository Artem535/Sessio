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

TEST_F(VideoSessionTest, PresenceBeforeJoinedIncludesAudioOnlyAndExcludesLocal) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  EXPECT_EQ(session.participants(), fake->participants());
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);
  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateParticipantJoined({"local", {}, "practitioner", true});
  fake->simulateParticipantJoined({"audio", {}, "client", false, true, false});
  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);
  EXPECT_EQ(session.participants()->remoteCount(), 1);
}

TEST_F(VideoSessionTest, OnlyLastRemoteDepartureReturnsToWaiting) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);
  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForParticipants);
  fake->simulateParticipantJoined({"a"});
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);
  stateSpy.clear();
  fake->simulateParticipantJoined({"a"});
  fake->simulateParticipantJoined({"b"});
  QCoreApplication::processEvents();
  EXPECT_EQ(session.participants()->remoteCount(), 2);
  fake->simulateParticipantLeft("a");
  fake->simulateParticipantLeft("a");
  fake->simulateParticipantLeft("unknown");
  fake->simulateParticipantJoined({"b", {}, {}, false, true, false});
  QCoreApplication::processEvents();
  EXPECT_EQ(session.participants()->remoteCount(), 1);
  EXPECT_EQ(session.state(), pcm::video::VideoSessionState::Connected);
  EXPECT_TRUE(stateSpy.isEmpty());
  fake->simulateParticipantLeft("b");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForParticipants);
}

TEST_F(VideoSessionTest, ReconnectAfterLastDepartureReturnsToWaitingAndStopsTimer) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake, std::chrono::milliseconds(30));
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);
  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateParticipantJoined({"a"});
  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);
  fake->simulateReconnecting();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Reconnecting);
  fake->simulateParticipantLeft("a");
  fake->simulateReconnected();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForParticipants);
  QSignalSpy failures(&session, &pcm::video::VideoSession::reconnectFailed);
  EXPECT_FALSE(failures.wait(80));
  EXPECT_EQ(session.state(), pcm::video::VideoSessionState::WaitingForParticipants);
  fake->simulateReconnecting();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Reconnecting);
  fake->simulateParticipantJoined({"b"});
  fake->simulateReconnected();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);
}

TEST_F(VideoSessionTest, TerminalFailureClearsLocalAndRemoteParticipants) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);
  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateParticipantJoined({"local", {}, {}, true});
  fake->simulateParticipantJoined({"a"});
  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);
  fake->simulateConnectionLost("ended");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Failed);
  EXPECT_EQ(session.participants()->rowCount(), 0);
  EXPECT_EQ(fake->mLeaveCallCount, 1);
}

TEST_F(VideoSessionTest, ExplicitLeaveClearsParticipantsBeforeProviderAcknowledges) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);
  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateParticipantJoined({"a"});
  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);
  session.leave();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Leaving);
  EXPECT_EQ(session.participants()->rowCount(), 0);
  fake->simulateLeft();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Ended);
  EXPECT_EQ(session.participants()->rowCount(), 0);
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

  // waitForState() only proves the session reached Joining eventually — it
  // says nothing about the path taken. Assert the recorded sequence
  // actually passed through Provisioned and PrejoinCheck, so a future
  // change that wires NoMeeting directly to Joining (skipping the two
  // pass-through states) would fail this test instead of passing it.
  //
  // The machine's entry into its own initial state (NoMeeting) is only
  // guaranteed to have happened by the time the FIRST event loop turn
  // runs after construction — join()'s queued dispatch (see join()'s own
  // comment) is what causes that first turn here, so stateSpy (constructed
  // before join() is called) also captures that leading NoMeeting entry.
  ASSERT_GE(stateSpy.count(), 4);
  EXPECT_EQ(stateSpy.at(0).at(0).value<pcm::video::VideoSessionState>(),
            pcm::video::VideoSessionState::NoMeeting);
  EXPECT_EQ(stateSpy.at(1).at(0).value<pcm::video::VideoSessionState>(),
            pcm::video::VideoSessionState::Provisioned);
  EXPECT_EQ(stateSpy.at(2).at(0).value<pcm::video::VideoSessionState>(),
            pcm::video::VideoSessionState::PrejoinCheck);
  EXPECT_EQ(stateSpy.at(3).at(0).value<pcm::video::VideoSessionState>(),
            pcm::video::VideoSessionState::Joining);
}

TEST_F(VideoSessionTest, JoinedThenRemoteParticipantConnectedReachesConnected) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);

  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForParticipants);

  fake->simulateParticipantJoined({"remote"});
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

TEST_F(VideoSessionTest, ReconnectingThenReconnectedReturnsToConnected) {
  // reconnecting() is the SDK's own "actively retrying" signal — the only
  // thing that should drive Connected -> Reconnecting. connectionLost() is
  // terminal (see the ConnectionLostFromConnected* tests below) and must
  // never enter Reconnecting.
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake, std::chrono::milliseconds(200));
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForParticipants);
  fake->simulateParticipantJoined({"remote"});
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);

  fake->simulateReconnecting();
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
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForParticipants);
  fake->simulateParticipantJoined({"remote"});
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);

  QSignalSpy reconnectFailedSpy(&session, &pcm::video::VideoSession::reconnectFailed);
  fake->simulateReconnecting();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Reconnecting);

  // No simulateReconnected() call — the 50ms timer must fire on its own.
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Failed);
  ASSERT_EQ(reconnectFailedSpy.count(), 1);
}

TEST_F(VideoSessionTest, ConnectionLostWhileReconnectingGoesToFailedImmediately) {
  // A terminal connectionLost() while already Reconnecting must pre-empt
  // the reconnect timer, not wait for it to time out.
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake, std::chrono::milliseconds(5000));
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForParticipants);
  fake->simulateParticipantJoined({"remote"});
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);

  fake->simulateReconnecting();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Reconnecting);

  fake->simulateConnectionLost("gave up");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Failed);
}

TEST_F(VideoSessionTest, ConnectionLostFromConnectedGoesDirectlyToFailed) {
  // A terminal disconnect that never went through a reconnecting() signal
  // at all (e.g. the server explicitly ended the room) must still reach
  // Failed — not get stuck in Connected waiting for a Reconnecting leg it
  // never entered.
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForParticipants);
  fake->simulateParticipantJoined({"remote"});
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);

  fake->simulateConnectionLost("room ended");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Failed);
}

TEST_F(VideoSessionTest, RemoteParticipantDisconnectedReturnsToWaitingForParticipants) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForParticipants);
  fake->simulateParticipantJoined({"remote"});
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);

  fake->simulateParticipantLeft("remote");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForParticipants);
}

TEST_F(VideoSessionTest, LeaveFromFailedIsSafelyIgnored) {
  // Entering Failed already tears the provider down (see VideoSession's
  // constructor). A caller's later leave() call has nothing left to do —
  // it must not hang or crash, and Failed has no transition to consume it.
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateJoinFailed("no route to host");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Failed);

  const int leaveCallCountOnEnteringFailed = fake->mLeaveCallCount;
  EXPECT_GE(leaveCallCountOnEnteringFailed, 1) << "entering Failed must tear the provider down";

  session.leave();
  QCoreApplication::processEvents();
  QCoreApplication::processEvents();
  EXPECT_EQ(session.state(), pcm::video::VideoSessionState::Failed);
}

TEST_F(VideoSessionTest, MediaErrorDoesNotAffectStateMachine) {
  // mediaError() reports a local capture/publish problem, never network
  // loss (see video_provider.h) — VideoSession has no transition on it
  // today (a future UI task, #80, is expected to surface it to the user),
  // so it must never move the state machine on its own.
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForParticipants);
  fake->simulateParticipantJoined({"remote"});
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);

  fake->simulateMediaError("camera unplugged");
  QCoreApplication::processEvents();
  QCoreApplication::processEvents();
  EXPECT_EQ(session.state(), pcm::video::VideoSessionState::Connected);
}

// Fixwave group 5, bug 1: VideoProvider::connectionLost()'s reason string
// used to be dropped on the floor — the state machine already reached
// Failed (see ConnectionLostFromConnectedGoesDirectlyToFailed above), but
// nothing relayed WHY. This proves VideoSession now relays it with a
// dedicated signal, the same pattern as joinFailed().
TEST_F(VideoSessionTest, ConnectionLostRelaysReasonWithSameSignal) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);
  QSignalSpy connectionLostSpy(&session, &pcm::video::VideoSession::connectionLost);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForParticipants);
  fake->simulateParticipantJoined({"remote"});
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);

  fake->simulateConnectionLost("room ended");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Failed);

  ASSERT_EQ(connectionLostSpy.count(), 1);
  EXPECT_EQ(connectionLostSpy.first().at(0).toString(), QStringLiteral("room ended"));
}

// Fixwave group 5, bug 1: mediaError() had no relay and no UI surface at
// all — this proves VideoSession now relays it, and confirms (again, this
// time via the relayed signal rather than state()) that it does not touch
// the state machine.
TEST_F(VideoSessionTest, MediaErrorRelaysReasonAndDoesNotChangeState) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);
  QSignalSpy mediaErrorSpy(&session, &pcm::video::VideoSession::mediaError);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForParticipants);
  fake->simulateParticipantJoined({"remote"});
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);

  // The relay connection is direct (same thread), so it has already fired
  // synchronously by the time simulateMediaError() returns — no event-loop
  // wait needed, matching how JoinFailureTransitionsToFailed above asserts
  // on joinFailedSpy immediately after waitForState() settles.
  fake->simulateMediaError("camera unplugged");
  ASSERT_EQ(mediaErrorSpy.count(), 1);
  EXPECT_EQ(mediaErrorSpy.first().at(0).toString(), QStringLiteral("camera unplugged"));
  EXPECT_EQ(session.state(), pcm::video::VideoSessionState::Connected);
}

TEST_F(VideoSessionTest, LeaveFromConnectedTransitionsToEnded) {
  auto *fake = new pcm::video::test::FakeVideoProvider();
  pcm::video::VideoSession session(fake);
  QSignalSpy stateSpy(&session, &pcm::video::VideoSession::stateChanged);

  session.join("wss://example.invalid", "token");
  waitForState(session, stateSpy, pcm::video::VideoSessionState::Joining);
  fake->simulateJoined();
  waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForParticipants);
  fake->simulateParticipantJoined({"remote"});
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
    waitForState(session, stateSpy, pcm::video::VideoSessionState::WaitingForParticipants);
    fake->simulateParticipantJoined({"remote"});
    waitForState(session, stateSpy, pcm::video::VideoSessionState::Connected);

    session.leave();
    waitForState(session, stateSpy, pcm::video::VideoSessionState::Leaving);
    fake->simulateLeft();
    waitForState(session, stateSpy, pcm::video::VideoSessionState::Ended);
  }
}
