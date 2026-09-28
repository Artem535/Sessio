#include "livekit_meeting_provider.h"
#include "fake_token_backend_server.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <gtest/gtest.h>

TEST(LiveKitMeetingProviderTest, CreateCallsTokenBackendAndEmitsCreatedDescriptor) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, R"({
    "meetingRef": "ref-9", "invitationUrl": "https://x/code-9", "passcode": "222222",
    "scheduledStart": "2026-10-01T10:00:00Z", "scheduledEnd": "2026-10-01T10:50:00Z"
  })");

  pcm::meeting::LiveKitMeetingProvider provider(server.baseUrl().toString(), "bearer-secret");
  QSignalSpy createdSpy(&provider, &pcm::meeting::MeetingProvider::created);

  provider.create({.scheduledStartIso = "2026-10-01T10:00:00Z",
                   .scheduledEndIso = "2026-10-01T10:50:00Z"});

  ASSERT_TRUE(createdSpy.wait(2000));
  const auto descriptor = createdSpy.at(0).at(0).value<pcm::meeting::MeetingDescriptor>();
  EXPECT_EQ(descriptor.kind, pcm::meeting::ProviderKind::LiveKit);
  EXPECT_EQ(descriptor.meetingRef, QStringLiteral("ref-9"));
  ASSERT_TRUE(descriptor.invitationState.has_value());
  EXPECT_EQ(*descriptor.invitationState, QStringLiteral("https://x/code-9|222222"));

  EXPECT_EQ(server.lastPath, QStringLiteral("/v1/meetings"));
  EXPECT_EQ(server.lastAuthorizationHeader, QStringLiteral("Bearer bearer-secret"));
  EXPECT_TRUE(server.lastBody.contains("2026-10-01T10:00:00Z"));
}

TEST(LiveKitMeetingProviderTest, CreateFailureEmitsCreateFailedWithServerMessage) {
  FakeTokenBackendServer server;
  server.setNextResponse(401, R"({"error": "unauthorized"})");

  pcm::meeting::LiveKitMeetingProvider provider(server.baseUrl().toString(), "wrong-secret");
  QSignalSpy failedSpy(&provider, &pcm::meeting::MeetingProvider::createFailed);

  provider.create({.scheduledStartIso = "2026-10-01T10:00:00Z",
                   .scheduledEndIso = "2026-10-01T10:50:00Z"});

  ASSERT_TRUE(failedSpy.wait(2000));
  EXPECT_EQ(failedSpy.at(0).at(0).toString(), QStringLiteral("unauthorized"));
}

TEST(LiveKitMeetingProviderTest, CancelCallsTokenBackendInvalidateAndEmitsCanceled) {
  FakeTokenBackendServer server;
  server.setNextResponse(204, QByteArray());

  pcm::meeting::LiveKitMeetingProvider provider(server.baseUrl().toString(), "bearer-secret");
  QSignalSpy canceledSpy(&provider, &pcm::meeting::MeetingProvider::canceled);

  provider.cancel("ref-9");

  ASSERT_TRUE(canceledSpy.wait(2000));
  EXPECT_EQ(server.lastPath, QStringLiteral("/v1/meetings/ref-9/invalidate"));
  EXPECT_EQ(server.lastAuthorizationHeader, QStringLiteral("Bearer bearer-secret"));
}

TEST(LiveKitMeetingProviderTest, CancelFailureEmitsCancelFailedWithServerMessage) {
  FakeTokenBackendServer server;
  server.setNextResponse(404, R"({"error": "request_failed"})");

  pcm::meeting::LiveKitMeetingProvider provider(server.baseUrl().toString(), "bearer-secret");
  QSignalSpy failedSpy(&provider, &pcm::meeting::MeetingProvider::cancelFailed);

  provider.cancel("unknown-ref");

  ASSERT_TRUE(failedSpy.wait(2000));
  EXPECT_EQ(failedSpy.at(0).at(0).toString(), QStringLiteral("request_failed"));
}

// Fixwave group 5, bug 3: without a restart, LiveKitMeetingProvider used to
// keep targeting whatever base URL/bearer credential it was constructed
// with, even after the specialist changed the token backend URL or the
// keychain credential changed. setTokenBackendBaseUrl()/setBearerCredential()
// must actually change what the NEXT create()/cancel() call targets/sends.
TEST(LiveKitMeetingProviderTest, SetTokenBackendBaseUrlRetargetsSubsequentRequests) {
  FakeTokenBackendServer firstServer;
  firstServer.setNextResponse(200, R"({"error": "should not be hit"})");
  FakeTokenBackendServer secondServer;
  secondServer.setNextResponse(200, R"({
    "meetingRef": "ref-2", "invitationUrl": "https://x/code-2", "passcode": "333333",
    "scheduledStart": "2026-10-01T10:00:00Z", "scheduledEnd": "2026-10-01T10:50:00Z"
  })");

  pcm::meeting::LiveKitMeetingProvider provider(firstServer.baseUrl().toString(), "bearer-secret");
  provider.setTokenBackendBaseUrl(secondServer.baseUrl().toString());

  QSignalSpy createdSpy(&provider, &pcm::meeting::MeetingProvider::created);
  provider.create({.scheduledStartIso = "2026-10-01T10:00:00Z",
                   .scheduledEndIso = "2026-10-01T10:50:00Z"});

  ASSERT_TRUE(createdSpy.wait(2000));
  EXPECT_TRUE(firstServer.lastPath.isEmpty()) << "the old base URL must not have been contacted";
  EXPECT_EQ(secondServer.lastPath, QStringLiteral("/v1/meetings"));
  const auto descriptor = createdSpy.at(0).at(0).value<pcm::meeting::MeetingDescriptor>();
  EXPECT_EQ(descriptor.meetingRef, QStringLiteral("ref-2"));
}

TEST(LiveKitMeetingProviderTest, SetBearerCredentialChangesSubsequentAuthorizationHeader) {
  FakeTokenBackendServer server;
  server.setNextResponse(204, QByteArray());

  pcm::meeting::LiveKitMeetingProvider provider(server.baseUrl().toString(), "old-secret");
  provider.setBearerCredential("new-secret");

  QSignalSpy canceledSpy(&provider, &pcm::meeting::MeetingProvider::canceled);
  provider.cancel("ref-9");

  ASSERT_TRUE(canceledSpy.wait(2000));
  EXPECT_EQ(server.lastAuthorizationHeader, QStringLiteral("Bearer new-secret"));
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
