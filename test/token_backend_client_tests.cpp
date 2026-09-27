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

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
