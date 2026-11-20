#include "app_role.h"

#include <gtest/gtest.h>

using pcm::config::AppRole;
using pcm::config::appRoleFromString;
using pcm::config::appRoleToString;

TEST(AppRoleTest, RoundTripsAllValues) {
  for (const auto role : {AppRole::Unset, AppRole::Specialist, AppRole::Client}) {
    const auto text = appRoleToString(role);
    const auto parsed = appRoleFromString(text);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, role);
  }
}

TEST(AppRoleTest, UnknownStringParsesToNullopt) {
  EXPECT_FALSE(appRoleFromString(QStringLiteral("Bogus")).has_value());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
