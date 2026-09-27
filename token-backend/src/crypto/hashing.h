#pragma once

#include <string>

namespace pcm::tokenbackend {

std::string fastHash(const std::string &plaintext);
bool fastHashMatches(const std::string &plaintext, const std::string &storedHashHex);

std::string hashPasscode(const std::string &passcode);
bool passcodeMatches(const std::string &passcode, const std::string &storedHash);

} // namespace pcm::tokenbackend
