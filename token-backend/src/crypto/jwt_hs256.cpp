#include "crypto/jwt_hs256.h"

#include <sodium.h>

#include <cstdint>

namespace pcm::tokenbackend {

namespace {

constexpr char kBase64UrlAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

} // namespace

std::string base64UrlEncode(const unsigned char *data, size_t len) {
  std::string out;
  out.reserve((len * 4 + 2) / 3);
  size_t i = 0;
  while (i + 3 <= len) {
    uint32_t triple = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
    out += kBase64UrlAlphabet[(triple >> 18) & 0x3F];
    out += kBase64UrlAlphabet[(triple >> 12) & 0x3F];
    out += kBase64UrlAlphabet[(triple >> 6) & 0x3F];
    out += kBase64UrlAlphabet[triple & 0x3F];
    i += 3;
  }
  size_t remaining = len - i;
  if (remaining == 1) {
    uint32_t val = data[i] << 16;
    out += kBase64UrlAlphabet[(val >> 18) & 0x3F];
    out += kBase64UrlAlphabet[(val >> 12) & 0x3F];
  } else if (remaining == 2) {
    uint32_t val = (data[i] << 16) | (data[i + 1] << 8);
    out += kBase64UrlAlphabet[(val >> 18) & 0x3F];
    out += kBase64UrlAlphabet[(val >> 12) & 0x3F];
    out += kBase64UrlAlphabet[(val >> 6) & 0x3F];
  }
  return out;
}

std::string base64UrlEncode(const std::string &s) {
  return base64UrlEncode(reinterpret_cast<const unsigned char *>(s.data()), s.size());
}

std::string jsonEscape(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += c;
  }
  return out;
}

std::string signHs256Jwt(const std::string &payloadJson, const std::string &secret) {
  std::string signingInput =
      base64UrlEncode(std::string(R"({"alg":"HS256","typ":"JWT"})")) + "." + base64UrlEncode(payloadJson);

  // crypto_auth_hmacsha256() (the one-shot form) requires an exactly
  // crypto_auth_hmacsha256_KEYBYTES (32-byte) key and reads out of bounds on
  // anything shorter — API secrets are arbitrary-length strings, so the
  // stateful init/update/final API is used instead: it implements real
  // HMAC key handling (short keys zero-padded, long keys pre-hashed) for any
  // key length, matching what a standard HMAC-SHA256 library would produce.
  unsigned char signature[crypto_auth_hmacsha256_BYTES];
  crypto_auth_hmacsha256_state state;
  crypto_auth_hmacsha256_init(&state, reinterpret_cast<const unsigned char *>(secret.data()),
                               secret.size());
  crypto_auth_hmacsha256_update(&state,
                                 reinterpret_cast<const unsigned char *>(signingInput.data()),
                                 signingInput.size());
  crypto_auth_hmacsha256_final(&state, signature);

  return signingInput + "." + base64UrlEncode(signature, sizeof(signature));
}

} // namespace pcm::tokenbackend
