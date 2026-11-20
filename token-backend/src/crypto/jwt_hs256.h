#pragma once

#include <cstddef>
#include <string>

namespace pcm::tokenbackend {

std::string base64UrlEncode(const unsigned char *data, size_t len);
std::string base64UrlEncode(const std::string &s);

// Escapes a string for embedding inside a JSON string literal.
std::string jsonEscape(const std::string &s);

// Signs `payloadJson` as a compact HS256 JWT (header {"alg":"HS256","typ":"JWT"}).
std::string signHs256Jwt(const std::string &payloadJson, const std::string &secret);

} // namespace pcm::tokenbackend
