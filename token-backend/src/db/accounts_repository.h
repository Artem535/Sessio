#pragma once

#include "db/sqlite_connection.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pcm::tokenbackend {

using AccountId = int64_t;

struct AccountSummary {
  AccountId id;
  std::string createdAt;
};

enum class RevokeResult { Removed, NotFound, InUse };

// Thread safety: every public method below takes SqliteConnection::lock() for
// its whole body, so calls from oat++'s worker threads are serialized against
// each other and against the other repositories sharing the same connection.
class AccountsRepository {
public:
  explicit AccountsRepository(SqliteConnection &conn) : conn_(conn) {}

  std::string seedAccount();
  std::optional<AccountId> findByCredential(const std::string &rawCredential);

  std::string createAccount();
  std::vector<AccountSummary> listAccounts();
  RevokeResult revokeAccount(AccountId id);

private:
  SqliteConnection &conn_;
};

} // namespace pcm::tokenbackend
