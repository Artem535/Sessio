#include "crypto/jitsi_jwt.h"

#include <gtest/gtest.h>
#include <sodium.h>

#include <sstream>
#include <vector>

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

bool contains(const std::string &hay, const std::string &needle) {
  return hay.find(needle) != std::string::npos;
}

} // namespace

class JitsiJwtTest : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_GE(sodium_init(), 0); }
};

TEST_F(JitsiJwtTest, MintsHs256WithJitsiClaims) {
  pcm::tokenbackend::JitsiUser user{"u1", "Анна", true};
  auto jwt = pcm::tokenbackend::mintJitsiJwt("sessio", "secret", "rm_abc", user, 600, 1000);
  auto parts = splitJwt(jwt);
  ASSERT_EQ(parts.size(), 3u);
  EXPECT_EQ(base64UrlDecode(parts[0]), R"({"alg":"HS256","typ":"JWT"})");

  auto payload = base64UrlDecode(parts[1]);
  EXPECT_TRUE(contains(payload, "\"aud\":\"jitsi\""));
  EXPECT_TRUE(contains(payload, "\"iss\":\"sessio\""));
  EXPECT_TRUE(contains(payload, "\"sub\":\"*\""));
  EXPECT_TRUE(contains(payload, "\"room\":\"rm_abc\""));
  EXPECT_TRUE(contains(payload, "\"exp\":1600"));
  EXPECT_TRUE(contains(payload, "\"nbf\":990"));
  EXPECT_TRUE(contains(payload, "\"id\":\"u1\""));
  EXPECT_TRUE(contains(payload, "\"name\":\"Анна\""));
  EXPECT_TRUE(contains(payload, "\"moderator\":true"));
}

TEST_F(JitsiJwtTest, SignatureVerifiesWithSecret) {
  pcm::tokenbackend::JitsiUser user{"u1", "Anna", false};
  auto jwt = pcm::tokenbackend::mintJitsiJwt("sessio", "short", "rm_abc", user, 600, 1000);
  auto parts = splitJwt(jwt);
  ASSERT_EQ(parts.size(), 3u);
  std::string signingInput = parts[0] + "." + parts[1];

  std::string secret = "short";
  unsigned char expected[crypto_auth_hmacsha256_BYTES];
  crypto_auth_hmacsha256_state state;
  crypto_auth_hmacsha256_init(&state, reinterpret_cast<const unsigned char *>(secret.data()),
                              secret.size());
  crypto_auth_hmacsha256_update(&state,
                                reinterpret_cast<const unsigned char *>(signingInput.data()),
                                signingInput.size());
  crypto_auth_hmacsha256_final(&state, expected);

  auto actual = base64UrlDecode(parts[2]);
  ASSERT_EQ(actual.size(), sizeof(expected));
  EXPECT_EQ(0, sodium_memcmp(expected, actual.data(), sizeof(expected)));
}

TEST_F(JitsiJwtTest, EscapesNameJson) {
  pcm::tokenbackend::JitsiUser user{"u1", "a\"b\\c", false};
  auto jwt = pcm::tokenbackend::mintJitsiJwt("sessio", "secret", "rm_abc", user, 600, 1000);
  auto payload = base64UrlDecode(splitJwt(jwt)[1]);
  EXPECT_TRUE(contains(payload, R"("name":"a\"b\\c")"));
}

TEST_F(JitsiJwtTest, RejectsEmptyRequiredFields) {
  pcm::tokenbackend::JitsiUser user{"u1", "A", false};
  EXPECT_THROW(pcm::tokenbackend::mintJitsiJwt("", "s", "rm", user, 60), std::invalid_argument);
  EXPECT_THROW(pcm::tokenbackend::mintJitsiJwt("a", "", "rm", user, 60), std::invalid_argument);
  EXPECT_THROW(pcm::tokenbackend::mintJitsiJwt("a", "s", "", user, 60), std::invalid_argument);
}
