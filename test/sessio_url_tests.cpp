#include "sessio_url.h"

#include <gtest/gtest.h>

TEST(SessioUrlTest, ParsesCodeAndPasscode) {
  const auto link = parseSessioJoinUrl("sessio://join?code=abc-123&passcode=654321");
  ASSERT_TRUE(link.has_value());
  EXPECT_EQ(link->code, QStringLiteral("abc-123"));
  EXPECT_EQ(link->passcode, QStringLiteral("654321"));
}

TEST(SessioUrlTest, WrongSchemeFailsToParse) {
  EXPECT_FALSE(parseSessioJoinUrl("https://join?code=abc&passcode=1").has_value());
}

TEST(SessioUrlTest, MissingPasscodeFailsToParse) {
  EXPECT_FALSE(parseSessioJoinUrl("sessio://join?code=abc-123").has_value());
}

TEST(SessioUrlTest, LinkWithoutBackendHasNoBackendUrl) {
  const auto link = parseSessioJoinUrl("sessio://join?code=abc-123&passcode=654321");
  ASSERT_TRUE(link.has_value());
  EXPECT_FALSE(link->backendUrl.has_value());
}

TEST(SessioUrlTest, ParsesPercentEncodedBackendUrl) {
  const auto link = parseSessioJoinUrl(
      "sessio://join?code=abc-123&passcode=654321"
      "&backend=https%3A%2F%2Ftoken.example.test%2Fapi%3Fregion%3Deu");
  ASSERT_TRUE(link.has_value());
  EXPECT_EQ(link->code, QStringLiteral("abc-123"));
  EXPECT_EQ(link->passcode, QStringLiteral("654321"));
  ASSERT_TRUE(link->backendUrl.has_value());
  EXPECT_EQ(*link->backendUrl, QStringLiteral("https://token.example.test/api?region=eu"));
}

TEST(SessioUrlTest, ParsesUnencodedBackendUrl) {
  const auto link = parseSessioJoinUrl(
      "sessio://join?code=abc&passcode=1&backend=http://127.0.0.1:8080");
  ASSERT_TRUE(link.has_value());
  ASSERT_TRUE(link->backendUrl.has_value());
  EXPECT_EQ(*link->backendUrl, QStringLiteral("http://127.0.0.1:8080"));
}

// Empty and non-http(s) values are treated as absent (std::nullopt), never as
// an empty-but-present backend, and never invalidate the code/passcode.
TEST(SessioUrlTest, EmptyBackendIsTreatedAsAbsent) {
  const auto link = parseSessioJoinUrl("sessio://join?code=abc&passcode=1&backend=");
  ASSERT_TRUE(link.has_value());
  EXPECT_EQ(link->code, QStringLiteral("abc"));
  EXPECT_FALSE(link->backendUrl.has_value());
}

TEST(SessioUrlTest, NonHttpBackendIsTreatedAsAbsent) {
  const auto link =
      parseSessioJoinUrl("sessio://join?code=abc&passcode=1&backend=file%3A%2F%2F%2Fetc%2Fpasswd");
  ASSERT_TRUE(link.has_value());
  EXPECT_FALSE(link->backendUrl.has_value());
}

TEST(SessioUrlTest, BackendAloneDoesNotMakeALinkValid) {
  EXPECT_FALSE(
      parseSessioJoinUrl("sessio://join?code=abc&backend=https%3A%2F%2Ftoken.example.test")
          .has_value());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
