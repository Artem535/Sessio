#include "token_backend_client.h"
#include "fake_token_backend_server.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
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

  client.requestSpecialistToken("secret-credential", "meeting-ref-1", " Анна \"A\" ");

  ASSERT_TRUE(receivedSpy.wait(2000));
  EXPECT_EQ(failedSpy.count(), 0);
  EXPECT_EQ(server.lastMethod, QStringLiteral("POST"));
  EXPECT_EQ(server.lastPath, QStringLiteral("/v1/meetings/meeting-ref-1/specialist-token"));
  EXPECT_EQ(server.lastAuthorizationHeader, QStringLiteral("Bearer secret-credential"));
  EXPECT_EQ(QJsonDocument::fromJson(server.lastBody).object().value("displayName").toString(), "Анна \"A\"");

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

  client.requestClientToken("code-abc", "123456", "Иван");

  ASSERT_TRUE(receivedSpy.wait(2000));
  EXPECT_EQ(server.lastPath, QStringLiteral("/v1/invitations/code-abc/client-token"));
  EXPECT_TRUE(server.lastAuthorizationHeader.isEmpty());
  EXPECT_TRUE(server.lastBody.contains("123456"));
  EXPECT_EQ(QJsonDocument::fromJson(server.lastBody).object().value("displayName").toString(), "Иван");
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

TEST(TokenBackendClientTest, SetBaseUrlRetargetsLaterRequests) {
  const QByteArray tokenBody = R"({
    "endpointUrl": "wss://livekit.example.test",
    "roomName": "room-4",
    "token": "jwt-4",
    "expiresAt": 999
  })";
  FakeTokenBackendServer oldServer;
  oldServer.setNextResponse(200, tokenBody);
  FakeTokenBackendServer newServer;
  newServer.setNextResponse(200, tokenBody);

  TokenBackendClient client(oldServer.baseUrl().toString());
  client.setBaseUrl(newServer.baseUrl().toString());
  EXPECT_EQ(client.baseUrl(), newServer.baseUrl().toString());

  QSignalSpy receivedSpy(&client, &TokenBackendClient::tokenReceived);
  client.requestClientToken("code-new", "123456");

  ASSERT_TRUE(receivedSpy.wait(2000));
  EXPECT_EQ(newServer.lastPath, QStringLiteral("/v1/invitations/code-new/client-token"));
  EXPECT_TRUE(oldServer.lastPath.isEmpty());
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// --- Schedule series routes (Task 4) ---

#include "fake_schedule_backend.h"

using pcm::tokenclient::ScheduleHttpResult;

namespace {
constexpr auto kUid = "11111111-2222-4333-8444-555555555555";
}

TEST(TokenBackendClientScheduleTest, PutSendsExactBodyBearerAndCorrelatesByUidAndRevision) {
  FakeScheduleBackend server;
  server.setFallback({.status = 200,
                      .body = R"({"series_uid":"11111111-2222-4333-8444-555555555555","revision":4,"content_hash":"abc"})"});
  TokenBackendClient client(server.baseUrl());
  QSignalSpy spy(&client, &TokenBackendClient::schedulePutFinished);

  client.putSchedule("cred", kUid, 4, R"({"revision":4})");

  ASSERT_TRUE(spy.wait(2000));
  ASSERT_EQ(server.requests().size(), 1);
  const auto &request = server.requests().first();
  EXPECT_EQ(request.method, "PUT");
  EXPECT_EQ(request.path, QStringLiteral("/v1/schedule-series/%1").arg(kUid));
  EXPECT_EQ(request.headers.value("authorization"), "Bearer cred");
  EXPECT_EQ(request.body, R"({"revision":4})");
  const auto result = spy.at(0).at(0).value<ScheduleHttpResult>();
  EXPECT_EQ(result.seriesUid, kUid);
  EXPECT_EQ(result.revision, 4);
  EXPECT_EQ(result.httpStatus, 200);
  EXPECT_TRUE(result.ok());
  EXPECT_FALSE(result.timedOut);
}

TEST(TokenBackendClientScheduleTest, ConflictCarriesStatusAndServerErrorCode) {
  FakeScheduleBackend server;
  server.setFallback({.status = 409, .body = R"({"error":"revision_conflict"})"});
  TokenBackendClient client(server.baseUrl());
  QSignalSpy spy(&client, &TokenBackendClient::schedulePutFinished);

  client.putSchedule("cred", kUid, 2, "{}");

  ASSERT_TRUE(spy.wait(2000));
  const auto result = spy.at(0).at(0).value<ScheduleHttpResult>();
  EXPECT_EQ(result.httpStatus, 409);
  EXPECT_EQ(result.errorCode, "revision_conflict");
  EXPECT_FALSE(result.ok());
}

TEST(TokenBackendClientScheduleTest, RetryAfterHeaderIsParsedAndBounded) {
  FakeScheduleBackend server;
  server.enqueue({.status = 429, .body = R"({"error":"too_many_attempts"})",
                  .headers = {{"Retry-After", "60"}}});
  server.enqueue({.status = 429, .body = "{}", .headers = {{"Retry-After", "999999"}}});
  TokenBackendClient client(server.baseUrl());
  QSignalSpy spy(&client, &TokenBackendClient::schedulePutFinished);

  client.putSchedule("cred", kUid, 1, "{}");
  ASSERT_TRUE(spy.wait(2000));
  EXPECT_EQ(spy.at(0).at(0).value<ScheduleHttpResult>().retryAfterSeconds, 60);
  client.putSchedule("cred", kUid, 1, "{}");
  ASSERT_TRUE(spy.wait(2000));
  EXPECT_EQ(spy.at(1).at(0).value<ScheduleHttpResult>().retryAfterSeconds, 3600);
}

TEST(TokenBackendClientScheduleTest, UnreachableBackendReportsNoHttpStatus) {
  TokenBackendClient client("http://127.0.0.1:1"); // nothing listens on port 1
  QSignalSpy spy(&client, &TokenBackendClient::schedulePutFinished);

  client.putSchedule("cred", kUid, 1, "{}");

  ASSERT_TRUE(spy.wait(5000));
  const auto result = spy.at(0).at(0).value<ScheduleHttpResult>();
  EXPECT_EQ(result.httpStatus, 0);
  EXPECT_FALSE(result.ok());
  EXPECT_FALSE(result.timedOut);
}

TEST(TokenBackendClientScheduleTest, SilentServerTimesOutInsteadOfHanging) {
  FakeScheduleBackend server;
  server.setFallback({.neverRespond = true});
  TokenBackendClient client(server.baseUrl());
  client.setScheduleRequestTimeoutMs(300);
  QSignalSpy spy(&client, &TokenBackendClient::schedulePutFinished);

  client.putSchedule("cred", kUid, 1, "{}");

  ASSERT_TRUE(spy.wait(3000));
  const auto result = spy.at(0).at(0).value<ScheduleHttpResult>();
  EXPECT_TRUE(result.timedOut);
  EXPECT_EQ(result.httpStatus, 0);
}

TEST(TokenBackendClientScheduleTest, DefaultRequestTimeoutIsFifteenSeconds) {
  TokenBackendClient client("http://127.0.0.1:1");
  EXPECT_EQ(client.scheduleRequestTimeoutMs(), 15000);
}

TEST(TokenBackendClientScheduleTest, GetScheduleReturnsBodyForRecovery) {
  FakeScheduleBackend server;
  server.setFallback({.status = 200, .body = R"({"revision":3,"content_hash":"h","snapshot":{}})"});
  TokenBackendClient client(server.baseUrl());
  QSignalSpy spy(&client, &TokenBackendClient::scheduleGetFinished);

  client.getSchedule("cred", kUid);

  ASSERT_TRUE(spy.wait(2000));
  EXPECT_EQ(server.requests().first().method, "GET");
  const auto result = spy.at(0).at(0).value<ScheduleHttpResult>();
  EXPECT_EQ(result.seriesUid, kUid);
  EXPECT_TRUE(result.body.contains("content_hash"));
}

TEST(TokenBackendClientScheduleTest, CapabilitiesDistinguishSupportedFromLegacyBackend) {
  FakeScheduleBackend server;
  server.enqueue({.status = 200, .body = R"({"scheduleSeries":true})"});
  server.enqueue({.status = 404, .body = R"({"error":"not_found"})"});
  TokenBackendClient client(server.baseUrl());
  QSignalSpy spy(&client, &TokenBackendClient::scheduleCapabilitiesFinished);

  client.requestScheduleCapabilities("cred");
  ASSERT_TRUE(spy.wait(2000));
  EXPECT_EQ(server.requests().first().path, "/v1/capabilities");
  EXPECT_EQ(server.requests().first().headers.value("authorization"), "Bearer cred");
  EXPECT_TRUE(spy.at(0).at(0).value<ScheduleHttpResult>().ok());
  EXPECT_TRUE(spy.at(0).at(0).value<ScheduleHttpResult>().body.contains("scheduleSeries"));

  client.requestScheduleCapabilities("cred");
  ASSERT_TRUE(spy.wait(2000));
  EXPECT_EQ(spy.at(1).at(0).value<ScheduleHttpResult>().httpStatus, 404);
}

TEST(TokenBackendClientScheduleTest, SeriesInvitationSendsIdempotencyKeyAndReissueFlag) {
  FakeScheduleBackend server;
  server.setFallback({.status = 200,
                      .body = R"({"invitation_url":"https://x/i/c","passcode":"p","generation":2})"});
  TokenBackendClient client(server.baseUrl());
  QSignalSpy spy(&client, &TokenBackendClient::seriesInvitationFinished);

  client.requestSeriesInvitation("cred", kUid, "key-1", true);

  ASSERT_TRUE(spy.wait(2000));
  const auto &request = server.requests().first();
  EXPECT_EQ(request.method, "POST");
  EXPECT_EQ(request.path, QStringLiteral("/v1/schedule-series/%1/invitation").arg(kUid));
  EXPECT_EQ(request.headers.value("idempotency-key"), "key-1");
  EXPECT_EQ(QJsonDocument::fromJson(request.body).object().value("reissue").toBool(), true);
  EXPECT_EQ(spy.at(0).at(0).value<ScheduleHttpResult>().seriesUid, kUid);
}

TEST(TokenBackendClientScheduleTest, OccurrenceSpecialistTokenTargetsExactOriginalStart) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, R"({
    "endpointUrl": "wss://livekit.example.test", "roomName": "room-occ",
    "token": "jwt-occ", "expiresAt": 5
  })");
  TokenBackendClient client(server.baseUrl().toString());
  QSignalSpy receivedSpy(&client, &TokenBackendClient::tokenReceived);

  // 2026-10-13T15:00:00Z, the key before any move of that occurrence.
  client.requestOccurrenceSpecialistToken("secret-credential", kUid, 1791903600000, " Анна ");

  ASSERT_TRUE(receivedSpy.wait(2000));
  EXPECT_EQ(server.lastMethod, QStringLiteral("POST"));
  EXPECT_EQ(server.lastPath,
            QStringLiteral("/v1/schedule-series/%1/occurrences/2026-10-13T15:00:00Z/specialist-token")
                .arg(kUid));
  EXPECT_EQ(server.lastAuthorizationHeader, QStringLiteral("Bearer secret-credential"));
  EXPECT_EQ(QJsonDocument::fromJson(server.lastBody).object().value("displayName").toString(), "Анна");
  EXPECT_EQ(receivedSpy.at(0).at(0).value<TokenResult>().roomName, QStringLiteral("room-occ"));
}

TEST(TokenBackendClientScheduleTest, OccurrenceSpecialistTokenFailureReportsServerReason) {
  FakeTokenBackendServer server;
  server.setNextResponse(409, R"({"error":"occurrence_unavailable"})");
  TokenBackendClient client(server.baseUrl().toString());
  QSignalSpy failedSpy(&client, &TokenBackendClient::tokenRequestFailed);

  client.requestOccurrenceSpecialistToken("cred", kUid, 1791903600000);

  ASSERT_TRUE(failedSpy.wait(2000));
  EXPECT_EQ(failedSpy.at(0).at(0).toString(), QStringLiteral("occurrence_unavailable"));
  EXPECT_TRUE(server.lastBody.isEmpty());
}

TEST(TokenBackendClientScheduleTest, OriginalStartIsFormattedWithSecondPrecisionAndZ) {
  EXPECT_EQ(pcm::tokenclient::formatOriginalStartUtc(1791903600000), "2026-10-13T15:00:00Z");
  EXPECT_EQ(pcm::tokenclient::formatOriginalStartUtc(1791903600999), "2026-10-13T15:00:00Z");
}
