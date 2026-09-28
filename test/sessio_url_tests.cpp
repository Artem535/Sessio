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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
