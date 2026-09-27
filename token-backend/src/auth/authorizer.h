#pragma once

#include "db/accounts_repository.h"

#include <optional>
#include <string>

namespace pcm::tokenbackend {

class Authorizer {
public:
  virtual ~Authorizer() = default;
  virtual std::optional<AccountId> authorize(const std::string &bearerCredential) = 0;
};

} // namespace pcm::tokenbackend
