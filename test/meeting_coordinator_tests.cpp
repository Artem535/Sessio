#include "meeting_coordinator.h"
#include "meeting_provider_test_listener.h"
#include "fake_token_backend_server.h"

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

// Fixwave group 5, bug 3: without a restart, the event editor's LiveKit
// meeting create/cancel flow used to keep targeting whatever token-backend
// base URL/bearer credential MeetingCoordinator was constructed with, even
// after the specialist changed either in Settings. Proves the coordinator's
// setters actually retarget the LiveKit provider it holds.
TEST(MeetingCoordinatorTest, SetTokenBackendBaseUrlRetargetsLiveKitProvider) {
  FakeTokenBackendServer firstServer;
  firstServer.setNextResponse(200, R"({"error": "should not be hit"})");
  FakeTokenBackendServer secondServer;
  secondServer.setNextResponse(200, R"({
    "meetingRef": "ref-3", "invitationUrl": "https://x/code-3", "passcode": "444444",
    "scheduledStart": "2026-10-01T10:00:00Z", "scheduledEnd": "2026-10-01T10:50:00Z"
  })");

  pcm::meeting::MeetingCoordinator coordinator(firstServer.baseUrl().toString(), "bearer-secret");
  coordinator.setTokenBackendBaseUrl(secondServer.baseUrl().toString());

  QSignalSpy createdSpy(&coordinator, &pcm::meeting::MeetingCoordinator::meetingCreated);
  coordinator.createMeeting(pcm::meeting::ProviderKind::LiveKit,
                            {.scheduledStartIso = "2026-10-01T10:00:00Z",
                             .scheduledEndIso = "2026-10-01T10:50:00Z"});

  ASSERT_TRUE(createdSpy.wait(2000));
  EXPECT_TRUE(firstServer.lastPath.isEmpty()) << "the old base URL must not have been contacted";
  EXPECT_EQ(secondServer.lastPath, QStringLiteral("/v1/meetings"));
}

TEST(MeetingCoordinatorTest, SetBearerCredentialRetargetsLiveKitProviderAuthorizationHeader) {
  FakeTokenBackendServer server;
  server.setNextResponse(204, QByteArray());

  pcm::meeting::MeetingCoordinator coordinator(server.baseUrl().toString(), "old-secret");
  coordinator.setBearerCredential("new-secret");

  QSignalSpy canceledSpy(&coordinator, &pcm::meeting::MeetingCoordinator::meetingCanceled);
  coordinator.cancelMeeting(pcm::meeting::ProviderKind::LiveKit, "ref-9");

  ASSERT_TRUE(canceledSpy.wait(2000));
  EXPECT_EQ(server.lastAuthorizationHeader, QStringLiteral("Bearer new-secret"));
}

// ExternalUrlMeetingProvider's override is the base class's no-op default —
// calling the coordinator's setters must be harmless and leave its behavior
// completely unchanged.
TEST(MeetingCoordinatorTest, SettersDoNotAffectExternalUrlProviderBehavior) {
  pcm::meeting::MeetingCoordinator coordinator("https://old.example.invalid", "old-secret");
  coordinator.setTokenBackendBaseUrl("https://new.example.invalid");
  coordinator.setBearerCredential("new-secret");

  MeetingSignalListener listener;
  QObject::connect(&coordinator, &pcm::meeting::MeetingCoordinator::meetingCreated, &listener,
                    &MeetingSignalListener::onCreated);

  coordinator.createMeeting(pcm::meeting::ProviderKind::ExternalUrl,
                            {.rawMeetingUrl = "https://meet.example.invalid/room-9"});

  ASSERT_TRUE(listener.lastDescriptor.has_value());
  EXPECT_EQ(listener.lastDescriptor->kind, pcm::meeting::ProviderKind::ExternalUrl);
  EXPECT_EQ(listener.lastDescriptor->meetingRef, QStringLiteral("https://meet.example.invalid/room-9"));
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

