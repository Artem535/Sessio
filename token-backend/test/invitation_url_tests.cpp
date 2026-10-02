#include "controller/invitation_url.h"

#include <gtest/gtest.h>

namespace pcm::tokenbackend {

TEST(InvitationUrlTest, ExpandsCodeAndPasscodeWithStandardFormatTemplate) {
  EXPECT_EQ(formatInvitationUrl(
                "sessio://join?code={}&passcode={}&backend=https%3A%2F%2Flivekit.sessio-pcm.ru",
                "osgxy4R9koM7Kw5mVBn84Eh-i0KNiipF", "123456"),
            "sessio://join?code=osgxy4R9koM7Kw5mVBn84Eh-i0KNiipF&passcode=123456&backend="
            "https%3A%2F%2Flivekit.sessio-pcm.ru");
}

TEST(InvitationUrlTest, KeepsLegacyPrefixConfigurationWorking) {
  EXPECT_EQ(formatInvitationUrl("https://join.example.test/j/", "code-1", "123456"),
            "https://join.example.test/j/code-1");
}

} // namespace pcm::tokenbackend
