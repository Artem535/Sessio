#include "token_backend_client.h"
#include "fake_token_backend_server.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <gtest/gtest.h>

using pcm::tokenclient::TokenBackendClient;
using pcm::tokenclient::TokenResult;

TEST(TokenBackendClientTest, SpecialistTokenSendsBearerHeaderAndParsesResponse) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, R"({
    "endpointUrl": "wss://livekit.example.test",
    "roomName": "room-1",
    "token": "jwt-1",
    "expiresAt": 999
  })");

  TokenBackendClient client(server.baseUrl().toString());
  QSignalSpy receivedSpy(&client, &TokenBackendClient::tokenReceived);
  QSignalSpy failedSpy(&client, &TokenBackendClient::tokenRequestFailed);

  client.requestSpecialistToken("secret-credential", "meeting-ref-1");

  ASSERT_TRUE(receivedSpy.wait(2000));
  EXPECT_EQ(failedSpy.count(), 0);
  EXPECT_EQ(server.lastMethod, QStringLiteral("POST"));
  EXPECT_EQ(server.lastPath, QStringLiteral("/v1/meetings/meeting-ref-1/specialist-token"));
  EXPECT_EQ(server.lastAuthorizationHeader, QStringLiteral("Bearer secret-credential"));

  const auto result = receivedSpy.at(0).at(0).value<TokenResult>();
  EXPECT_EQ(result.roomName, QStringLiteral("room-1"));
}

TEST(TokenBackendClientTest, ClientTokenSendsPasscodeBodyWithNoAuthHeader) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, R"({
    "endpointUrl": "wss://livekit.example.test",
    "roomName": "room-2",
    "token": "jwt-2",
    "expiresAt": 999
  })");

  TokenBackendClient client(server.baseUrl().toString());
  QSignalSpy receivedSpy(&client, &TokenBackendClient::tokenReceived);

  client.requestClientToken("code-abc", "123456");

  ASSERT_TRUE(receivedSpy.wait(2000));
  EXPECT_EQ(server.lastPath, QStringLiteral("/v1/invitations/code-abc/client-token"));
  EXPECT_TRUE(server.lastAuthorizationHeader.isEmpty());
  EXPECT_TRUE(server.lastBody.contains("123456"));
}

TEST(TokenBackendClientTest, ErrorStatusEmitsTokenRequestFailedWithServerMessage) {
  FakeTokenBackendServer server;
  server.setNextResponse(401, R"({"error": "wrong_passcode"})");

  TokenBackendClient client(server.baseUrl().toString());
  QSignalSpy failedSpy(&client, &TokenBackendClient::tokenRequestFailed);

  client.requestClientToken("code-abc", "000000");

  ASSERT_TRUE(failedSpy.wait(2000));
  EXPECT_EQ(failedSpy.at(0).at(0).toString(), QStringLiteral("wrong_passcode"));
}

TEST(TokenBackendClientTest, ClientTokenEscapesPasscodeWithQuoteCharacter) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, R"({
    "endpointUrl": "wss://livekit.example.test",
    "roomName": "room-3",
    "token": "jwt-3",
    "expiresAt": 999
  })");

  TokenBackendClient client(server.baseUrl().toString());
  QSignalSpy receivedSpy(&client, &TokenBackendClient::tokenReceived);

  const QString passcodeWithQuote = R"(pass"code)";
  client.requestClientToken("code-def", passcodeWithQuote);

  ASSERT_TRUE(receivedSpy.wait(2000));
  // Verify the body is valid JSON: should contain the escaped quote
  EXPECT_TRUE(server.lastBody.contains(R"(\")"));
  EXPECT_TRUE(server.lastBody.contains("pass"));
  EXPECT_TRUE(server.lastBody.contains("code"));
}

TEST(TokenBackendClientTest, CreateMeetingSendsScheduleAndBearerHeader) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, R"({
    "meetingRef": "ref-1", "invitationUrl": "https://x/code-1", "passcode": "111111",
    "scheduledStart": "2026-10-01T10:00:00Z", "scheduledEnd": "2026-10-01T10:50:00Z"
  })");

  pcm::tokenclient::TokenBackendClient client(server.baseUrl().toString());
  QSignalSpy createdSpy(&client, &pcm::tokenclient::TokenBackendClient::meetingCreated);

  client.requestCreateMeeting("secret", "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");

  ASSERT_TRUE(createdSpy.wait(2000));
  EXPECT_EQ(server.lastPath, QStringLiteral("/v1/meetings"));
  EXPECT_EQ(server.lastAuthorizationHeader, QStringLiteral("Bearer secret"));
  EXPECT_TRUE(server.lastBody.contains("2026-10-01T10:00:00Z"));

  const auto result =
      createdSpy.at(0).at(0).value<pcm::tokenclient::MeetingCreateResult>();
  EXPECT_EQ(result.meetingRef, QStringLiteral("ref-1"));
  EXPECT_EQ(result.invitationUrl, QStringLiteral("https://x/code-1"));
  EXPECT_EQ(result.passcode, QStringLiteral("111111"));
}

TEST(TokenBackendClientTest, CreateMeetingErrorStatusEmitsMeetingCreateFailed) {
  FakeTokenBackendServer server;
  server.setNextResponse(401, R"({"error": "unauthorized"})");

  pcm::tokenclient::TokenBackendClient client(server.baseUrl().toString());
  QSignalSpy failedSpy(&client, &pcm::tokenclient::TokenBackendClient::meetingCreateFailed);

  client.requestCreateMeeting("bad-secret", "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");

  ASSERT_TRUE(failedSpy.wait(2000));
  EXPECT_EQ(failedSpy.at(0).at(0).toString(), QStringLiteral("unauthorized"));
}

TEST(TokenBackendClientTest, InvalidateMeetingSendsBearerHeaderToInvalidateRoute) {
  FakeTokenBackendServer server;
  server.setNextResponse(204, QByteArray());

  pcm::tokenclient::TokenBackendClient client(server.baseUrl().toString());
  QSignalSpy invalidatedSpy(&client, &pcm::tokenclient::TokenBackendClient::meetingInvalidated);

  client.requestInvalidateMeeting("secret", "ref-1");

  ASSERT_TRUE(invalidatedSpy.wait(2000));
  EXPECT_EQ(server.lastPath, QStringLiteral("/v1/meetings/ref-1/invalidate"));
  EXPECT_EQ(server.lastAuthorizationHeader, QStringLiteral("Bearer secret"));
}

TEST(TokenBackendClientTest, InvalidateMeetingErrorStatusEmitsMeetingInvalidateFailed) {
  FakeTokenBackendServer server;
  server.setNextResponse(404, R"({"error": "request_failed"})");

  pcm::tokenclient::TokenBackendClient client(server.baseUrl().toString());
  QSignalSpy failedSpy(&client, &pcm::tokenclient::TokenBackendClient::meetingInvalidateFailed);

  client.requestInvalidateMeeting("secret", "unknown-ref");

  ASSERT_TRUE(failedSpy.wait(2000));
  EXPECT_EQ(failedSpy.at(0).at(0).toString(), QStringLiteral("request_failed"));
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
