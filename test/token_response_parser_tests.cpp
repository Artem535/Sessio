#include "token_response_parser.h"

#include <gtest/gtest.h>

using pcm::tokenclient::parseErrorMessage;
using pcm::tokenclient::parseTokenResponse;

TEST(TokenResponseParserTest, ParsesWellFormedTokenResponse) {
  const QByteArray json = R"({
    "endpointUrl": "wss://livekit.example.test",
    "roomName": "room-42",
    "token": "eyJhbGciOi...",
    "expiresAt": 1234567890
  })";

  const auto result = parseTokenResponse(json);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->endpointUrl, QStringLiteral("wss://livekit.example.test"));
  EXPECT_EQ(result->roomName, QStringLiteral("room-42"));
  EXPECT_EQ(result->token, QStringLiteral("eyJhbGciOi..."));
  EXPECT_EQ(result->expiresAt, 1234567890);
}

TEST(TokenResponseParserTest, MissingFieldFailsToParse) {
  const QByteArray json = R"({"endpointUrl": "wss://x", "roomName": "r"})";
  EXPECT_FALSE(parseTokenResponse(json).has_value());
}

TEST(TokenResponseParserTest, MalformedJsonFailsToParse) {
  EXPECT_FALSE(parseTokenResponse("not json").has_value());
}

TEST(TokenResponseParserTest, ParsesErrorMessage) {
  const QByteArray json = R"({"error": "wrong_passcode"})";
  EXPECT_EQ(parseErrorMessage(json), QStringLiteral("wrong_passcode"));
}

TEST(TokenResponseParserTest, UnparsableErrorBodyFallsBackToGenericMessage) {
  EXPECT_EQ(parseErrorMessage("not json"), QStringLiteral("request_failed"));
}

TEST(TokenResponseParserTest, ParsesMeetingCreateResponse) {
  const QByteArray json = R"({
    "meetingRef": "ref-1",
    "invitationUrl": "https://invite.example.test/code-1",
    "passcode": "123456",
    "scheduledStart": "2026-10-01T10:00:00Z",
    "scheduledEnd": "2026-10-01T10:50:00Z"
  })";
  const auto result = pcm::tokenclient::parseMeetingCreateResponse(json);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->meetingRef, QStringLiteral("ref-1"));
  EXPECT_EQ(result->invitationUrl, QStringLiteral("https://invite.example.test/code-1"));
  EXPECT_EQ(result->passcode, QStringLiteral("123456"));
}

TEST(TokenResponseParserTest, MeetingCreateResponseMissingFieldFailsToParse) {
  const QByteArray json = R"({"meetingRef": "ref-1", "invitationUrl": "https://x"})";
  EXPECT_FALSE(pcm::tokenclient::parseMeetingCreateResponse(json).has_value());
}

TEST(TokenResponseParserTest, MeetingCreateResponseMalformedJsonFailsToParse) {
  EXPECT_FALSE(pcm::tokenclient::parseMeetingCreateResponse("not json").has_value());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
