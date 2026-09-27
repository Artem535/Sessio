#include "crypto/hashing.h"
#include "crypto/random_token.h"

#include <gtest/gtest.h>
#include <sodium.h>

class CryptoTest : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(sodium_init() >= 0, true); }
};

TEST_F(CryptoTest, GenerateUrlSafeTokenHasNoPaddingOrSlashes) {
  auto token = pcm::tokenbackend::generateUrlSafeToken(24);
  EXPECT_FALSE(token.empty());
  EXPECT_EQ(token.find('='), std::string::npos);
  EXPECT_EQ(token.find('/'), std::string::npos);
  EXPECT_EQ(token.find('+'), std::string::npos);
}

TEST_F(CryptoTest, GenerateNumericPasscodeIsSixDigits) {
  auto passcode = pcm::tokenbackend::generateNumericPasscode();
  ASSERT_EQ(passcode.size(), 6u);
  for (char c : passcode) {
    EXPECT_TRUE(c >= '0' && c <= '9');
  }
}

TEST_F(CryptoTest, FastHashIsDeterministic) {
  auto a = pcm::tokenbackend::fastHash("same-input");
  auto b = pcm::tokenbackend::fastHash("same-input");
  EXPECT_EQ(a, b);
}

TEST_F(CryptoTest, FastHashMatchesVerifiesCorrectly) {
  auto hash = pcm::tokenbackend::fastHash("token-value");
  EXPECT_TRUE(pcm::tokenbackend::fastHashMatches("token-value", hash));
  EXPECT_FALSE(pcm::tokenbackend::fastHashMatches("wrong-value", hash));
}

TEST_F(CryptoTest, PasscodeHashRoundTrips) {
  auto hash = pcm::tokenbackend::hashPasscode("123456");
  EXPECT_TRUE(pcm::tokenbackend::passcodeMatches("123456", hash));
  EXPECT_FALSE(pcm::tokenbackend::passcodeMatches("000000", hash));
}

TEST_F(CryptoTest, PasscodeHashIsRandomizedPerCall) {
  auto first = pcm::tokenbackend::hashPasscode("123456");
  auto second = pcm::tokenbackend::hashPasscode("123456");
  EXPECT_NE(first, second) << "Argon2id must salt each call independently";
}

// The unauthenticated client-token endpoint verifies a passcode on every
// request, so the Argon2id memory limit is attacker-controlled allocation.
// Pin it to INTERACTIVE (64 MiB, m=65536) so a request burst cannot exhaust
// the shared VPS's RAM. Raising this back to MODERATE (256 MiB) must be a
// deliberate decision, not an accident.
TEST_F(CryptoTest, PasscodeHashUsesInteractiveCostParameters) {
  auto hash = pcm::tokenbackend::hashPasscode("123456");
  EXPECT_NE(hash.find("$argon2id$"), std::string::npos) << hash;
  EXPECT_NE(hash.find("m=65536"), std::string::npos)
      << "expected 64 MiB memory limit (INTERACTIVE), got: " << hash;
  EXPECT_NE(hash.find("t=2"), std::string::npos)
      << "expected opslimit 2 (INTERACTIVE), got: " << hash;
}
