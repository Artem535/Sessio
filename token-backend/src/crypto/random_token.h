#pragma once

#include <string>

namespace pcm::tokenbackend {
std::string generateUrlSafeToken(size_t numRandomBytes);
std::string generateNumericPasscode();
} // namespace pcm::tokenbackend
