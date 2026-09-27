#include "crypto/livekit_jwt.h"

#include <gtest/gtest.h>
#include <sodium.h>

#include <sstream>

namespace {

std::string base64UrlDecode(const std::string &input) {
  static const std::string alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::vector<int> table(256, -1);
  for (size_t i = 0; i < alphabet.size(); ++i) {
    table[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
  }
  std::string out;
  int buffer = 0, bitsCollected = 0;
  for (char c : input) {
    int value = table[static_cast<unsigned char>(c)];
    if (value < 0) continue;
    buffer = (buffer << 6) | value;
    bitsCollected += 6;
    if (bitsCollected >= 8) {
      bitsCollected -= 8;
      out += static_cast<char>((buffer >> bitsCollected) & 0xFF);
    }
  }
  return out;
}

std::vector<std::string> splitJwt(const std::string &jwt) {
  std::vector<std::string> parts;
  std::stringstream ss(jwt);
  std::string part;
  while (std::getline(ss, part, '.')) {
    parts.push_back(part);
  }
  return parts;
}

} // namespace

class LiveKitJwtTest : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(sodium_init() >= 0, true); }
};

TEST_F(LiveKitJwtTest, HasThreeDotSeparatedParts) {
  pcm::tokenbackend::VideoGrants grants;
  grants.room = "rm_test";
  auto jwt = pcm::tokenbackend::mintLiveKitJwt("api-key", "api-secret", "identity-1", grants, 60);
  EXPECT_EQ(splitJwt(jwt).size(), 3u);
}

TEST_F(LiveKitJwtTest, PayloadContainsExpectedClaims) {
  pcm::tokenbackend::VideoGrants grants;
  grants.room = "rm_9f7e21";
  auto jwt = pcm::tokenbackend::mintLiveKitJwt("my-key", "my-secret", "client-mtg_a1b2c3", grants, 60);
  auto parts = splitJwt(jwt);
  auto payload = base64UrlDecode(parts[1]);

  EXPECT_NE(payload.find("\"iss\":\"my-key\""), std::string::npos);
  EXPECT_NE(payload.find("\"sub\":\"client-mtg_a1b2c3\""), std::string::npos);
  EXPECT_NE(payload.find("\"room\":\"rm_9f7e21\""), std::string::npos);
  EXPECT_NE(payload.find("\"roomJoin\":true"), std::string::npos);
}

TEST_F(LiveKitJwtTest, SignatureVerifiesWithCorrectSecret) {
  pcm::tokenbackend::VideoGrants grants;
  grants.room = "rm_test";
  auto jwt = pcm::tokenbackend::mintLiveKitJwt("api-key", "correct-secret", "identity-1", grants, 60);
  auto parts = splitJwt(jwt);
  std::string signingInput = parts[0] + "." + parts[1];

  std::string secret = "correct-secret";
  unsigned char expected[crypto_auth_hmacsha256_BYTES];
  crypto_auth_hmacsha256_state state;
  crypto_auth_hmacsha256_init(&state, reinterpret_cast<const unsigned char *>(secret.data()),
                               secret.size());
  crypto_auth_hmacsha256_update(&state,
                                 reinterpret_cast<const unsigned char *>(signingInput.data()),
                                 signingInput.size());
  crypto_auth_hmacsha256_final(&state, expected);

  auto actualSig = base64UrlDecode(parts[2]);
  ASSERT_EQ(actualSig.size(), sizeof(expected));
  EXPECT_EQ(0, sodium_memcmp(expected, actualSig.data(), sizeof(expected)));
}

TEST_F(LiveKitJwtTest, RejectsEmptyRoom) {
  pcm::tokenbackend::VideoGrants grants;
  grants.room = "";
  EXPECT_THROW(pcm::tokenbackend::mintLiveKitJwt("k", "s", "identity", grants, 60),
               std::invalid_argument);
}
