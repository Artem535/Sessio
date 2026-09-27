#pragma once

#include "auth/authorizer.h"
#include "db/accounts_repository.h"

#include <cctype>
#include <string>

namespace pcm::tokenbackend {

// Strips the RFC 7235 `Bearer ` scheme prefix (case-insensitive, as the RFC
// requires) plus surrounding whitespace from an Authorization header value.
//
// Without this, a client sending the standard `Authorization: Bearer <cred>`
// has the literal string "Bearer <cred>" hashed, which never matches the
// stored credential hash — so the correct credential silently 401s and only
// the non-standard bare form works. The bare form stays supported: the prefix
// is accepted, not required.
inline std::string stripBearerPrefix(const std::string &headerValue) {
  const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };

  size_t begin = 0;
  while (begin < headerValue.size() && isSpace(headerValue[begin])) {
    ++begin;
  }
  size_t end = headerValue.size();
  while (end > begin && isSpace(headerValue[end - 1])) {
    --end;
  }

  static constexpr char kScheme[] = "bearer";
  static constexpr size_t kSchemeLen = sizeof(kScheme) - 1;

  if (end - begin > kSchemeLen) {
    bool matches = true;
    for (size_t i = 0; i < kSchemeLen; ++i) {
      if (std::tolower(static_cast<unsigned char>(headerValue[begin + i])) != kScheme[i]) {
        matches = false;
        break;
      }
    }
    // The scheme must be followed by whitespace, so a credential that merely
    // starts with "bearer" (e.g. "bearerToken123") is not truncated.
    if (matches && isSpace(headerValue[begin + kSchemeLen])) {
      begin += kSchemeLen;
      while (begin < end && isSpace(headerValue[begin])) {
        ++begin;
      }
    }
  }

  return headerValue.substr(begin, end - begin);
}

class StaticTokenAuthorizer : public Authorizer {
public:
  explicit StaticTokenAuthorizer(AccountsRepository &accounts) : accounts_(accounts) {}
  std::optional<AccountId> authorize(const std::string &bearerCredential) override {
    auto credential = stripBearerPrefix(bearerCredential);
    if (credential.empty()) {
      return std::nullopt;
    }
    return accounts_.findByCredential(credential);
  }

private:
  AccountsRepository &accounts_;
};

} // namespace pcm::tokenbackend
