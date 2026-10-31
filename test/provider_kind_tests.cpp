#include "provider_kind.h"

#include <gtest/gtest.h>

TEST(ProviderKindTest, RoundTripsExternalUrl) {
  const auto text = pcm::meeting::providerKindToString(pcm::meeting::ProviderKind::ExternalUrl);
  EXPECT_EQ(text, "ExternalUrl");
  const auto parsed = pcm::meeting::providerKindFromString(text);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(*parsed, pcm::meeting::ProviderKind::ExternalUrl);
}

TEST(ProviderKindTest, RoundTripsLiveKit) {
  const auto text = pcm::meeting::providerKindToString(pcm::meeting::ProviderKind::LiveKit);
  EXPECT_EQ(text, "LiveKit");
  const auto parsed = pcm::meeting::providerKindFromString(text);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(*parsed, pcm::meeting::ProviderKind::LiveKit);
}

TEST(ProviderKindTest, JitsiRoundTripsAsString) {
  const auto text = pcm::meeting::providerKindToString(pcm::meeting::ProviderKind::Jitsi);
  EXPECT_EQ(text, "Jitsi");
  const auto parsed = pcm::meeting::providerKindFromString(text);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(*parsed, pcm::meeting::ProviderKind::Jitsi);
}

TEST(ProviderKindTest, isBackendHostedCoversLiveKitAndJitsi) {
  using pcm::meeting::ProviderKind;
  EXPECT_TRUE(pcm::meeting::isBackendHosted(ProviderKind::LiveKit));
  EXPECT_TRUE(pcm::meeting::isBackendHosted(ProviderKind::Jitsi));
  EXPECT_FALSE(pcm::meeting::isBackendHosted(ProviderKind::ExternalUrl));
}

TEST(ProviderKindTest, UnknownStringParsesToNullopt) {
  EXPECT_FALSE(pcm::meeting::providerKindFromString("SomethingElse").has_value());
  EXPECT_FALSE(pcm::meeting::providerKindFromString("").has_value());
}
TEST(ProviderKindTest, ExternalJitsiRoundTripsWithoutBackendOwnership) {
  using namespace pcm::meeting;
  EXPECT_EQ(providerKindFromString(providerKindToString(ProviderKind::ExternalJitsi)), ProviderKind::ExternalJitsi);
  EXPECT_FALSE(isBackendHosted(ProviderKind::ExternalJitsi));
}
