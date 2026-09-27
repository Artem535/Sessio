#include "crypto/random_token.h"

#include <sodium.h>

#include <iomanip>
#include <sstream>
#include <vector>

namespace pcm::tokenbackend {

namespace {
constexpr char kBase64UrlAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
}

std::string generateUrlSafeToken(size_t numRandomBytes) {
  std::vector<unsigned char> bytes(numRandomBytes);
  randombytes_buf(bytes.data(), bytes.size());

  std::string out;
  out.reserve((numRandomBytes * 4 + 2) / 3);
  size_t i = 0;
  while (i + 3 <= bytes.size()) {
    uint32_t triple = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
    out += kBase64UrlAlphabet[(triple >> 18) & 0x3F];
    out += kBase64UrlAlphabet[(triple >> 12) & 0x3F];
    out += kBase64UrlAlphabet[(triple >> 6) & 0x3F];
    out += kBase64UrlAlphabet[triple & 0x3F];
    i += 3;
  }
  size_t remaining = bytes.size() - i;
  if (remaining == 1) {
    uint32_t val = bytes[i] << 16;
    out += kBase64UrlAlphabet[(val >> 18) & 0x3F];
    out += kBase64UrlAlphabet[(val >> 12) & 0x3F];
  } else if (remaining == 2) {
    uint32_t val = (bytes[i] << 16) | (bytes[i + 1] << 8);
    out += kBase64UrlAlphabet[(val >> 18) & 0x3F];
    out += kBase64UrlAlphabet[(val >> 12) & 0x3F];
    out += kBase64UrlAlphabet[(val >> 6) & 0x3F];
  }
  return out;
}

std::string generateNumericPasscode() {
  uint32_t value = randombytes_uniform(1000000);
  std::ostringstream oss;
  oss << std::setw(6) << std::setfill('0') << value;
  return oss.str();
}

} // namespace pcm::tokenbackend
