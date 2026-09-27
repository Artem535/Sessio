#include "meeting_coordinator.h"
#include "meeting_provider_test_listener.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <gtest/gtest.h>

using pcm::meeting::test::MeetingSignalListener;

TEST(MeetingCoordinatorTest, DispatchesCreateToExternalUrlProvider) {
  pcm::meeting::MeetingCoordinator coordinator("", "");
  MeetingSignalListener listener;
  QObject::connect(&coordinator, &pcm::meeting::MeetingCoordinator::meetingCreated, &listener,
                    &MeetingSignalListener::onCreated);
  QObject::connect(&coordinator, &pcm::meeting::MeetingCoordinator::meetingCreateFailed, &listener,
                    &MeetingSignalListener::onCreateFailed);

  coordinator.createMeeting(pcm::meeting::ProviderKind::ExternalUrl,
                            {.rawMeetingUrl = "https://meet.example.invalid/room-9"});

  ASSERT_TRUE(listener.lastDescriptor.has_value());
  EXPECT_FALSE(listener.lastCreateError.has_value());
  EXPECT_EQ(listener.lastDescriptor->kind, pcm::meeting::ProviderKind::ExternalUrl);
  EXPECT_EQ(listener.lastDescriptor->meetingRef, QStringLiteral("https://meet.example.invalid/room-9"));
}

TEST(MeetingCoordinatorTest, DispatchesCreateToLiveKitProviderWhichFails) {
  // Base URL/credential are empty (real ones are threaded through in Task
  // 17), so LiveKitMeetingProvider's real HTTP call below fails against an
  // unreachable/invalid endpoint rather than never being attempted. Unlike
  // the old stub, that failure arrives asynchronously, so this test needs to
  // spin an event loop rather than asserting immediately after the call.
  pcm::meeting::MeetingCoordinator coordinator("", "");
  MeetingSignalListener listener;
  QObject::connect(&coordinator, &pcm::meeting::MeetingCoordinator::meetingCreated, &listener,
                    &MeetingSignalListener::onCreated);
  QObject::connect(&coordinator, &pcm::meeting::MeetingCoordinator::meetingCreateFailed, &listener,
                    &MeetingSignalListener::onCreateFailed);
  QSignalSpy failedSpy(&coordinator, &pcm::meeting::MeetingCoordinator::meetingCreateFailed);

  coordinator.createMeeting(pcm::meeting::ProviderKind::LiveKit, {.rawMeetingUrl = ""});

  ASSERT_TRUE(failedSpy.wait(2000));
  EXPECT_FALSE(listener.lastDescriptor.has_value());
  ASSERT_TRUE(listener.lastCreateError.has_value());
  EXPECT_FALSE(listener.lastCreateError->isEmpty());
}

TEST(MeetingCoordinatorTest, DispatchesCancelToExternalUrlProvider) {
  pcm::meeting::MeetingCoordinator coordinator("", "");
  MeetingSignalListener listener;
  QObject::connect(&coordinator, &pcm::meeting::MeetingCoordinator::meetingCanceled, &listener,
                    &MeetingSignalListener::onCanceled);

  coordinator.cancelMeeting(pcm::meeting::ProviderKind::ExternalUrl,
                            "https://meet.example.invalid/room-9");

  EXPECT_TRUE(listener.cancelSucceeded);
}

TEST(MeetingCoordinatorTest, DispatchesCancelToLiveKitProviderWhichFails) {
  // Same async-vs-sync note as DispatchesCreateToLiveKitProviderWhichFails
  // above: the real provider's failure now arrives via a signal after the
  // event loop runs, not synchronously within cancelMeeting().
  pcm::meeting::MeetingCoordinator coordinator("", "");
  MeetingSignalListener listener;
  QObject::connect(&coordinator, &pcm::meeting::MeetingCoordinator::meetingCancelFailed, &listener,
                    &MeetingSignalListener::onCancelFailed);
  QSignalSpy failedSpy(&coordinator, &pcm::meeting::MeetingCoordinator::meetingCancelFailed);

  coordinator.cancelMeeting(pcm::meeting::ProviderKind::LiveKit, "some-ref");

  ASSERT_TRUE(failedSpy.wait(2000));
  ASSERT_TRUE(listener.lastCancelError.has_value());
  EXPECT_FALSE(listener.lastCancelError->isEmpty());
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

