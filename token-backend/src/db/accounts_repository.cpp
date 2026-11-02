#include "db/accounts_repository.h"
#include "crypto/hashing.h"
#include "crypto/random_token.h"

#include <chrono>
#include <stdexcept>

namespace pcm::tokenbackend {

namespace {
std::string nowIso8601() {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
  return buf;
}
} // namespace

std::string AccountsRepository::seedAccount() {
  // Held for the whole body, so the transaction below cannot have another
  // thread's statement land inside it. See SqliteConnection::lock().
  auto guard = conn_.lock();

  auto credential = generateUrlSafeToken(32);
  auto hash = fastHash(credential);

  // DELETE-then-INSERT: without a transaction a failure between the two (or a
  // concurrent reader landing between them) leaves the service with no account
  // at all, 401-ing every request with no way back except another seed run.
  SqliteTransaction tx(conn_);

  conn_.exec("DELETE FROM accounts;");

  sqlite3_stmt *stmt = nullptr;
  const char *sql = "INSERT INTO accounts (credential_hash, created_at) VALUES (?, ?);";
  if (sqlite3_prepare_v2(conn_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare account insert");
  }
  auto createdAt = nowIso8601();
  sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, createdAt.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error("failed to insert account");
  }
  sqlite3_finalize(stmt);

  tx.commit();
  return credential;
}

std::string AccountsRepository::createAccount() {
  // Held for the whole body, matching every other public entry point.
  auto guard = conn_.lock();

  auto credential = generateUrlSafeToken(32);
  auto hash = fastHash(credential);

  // Single INSERT, no preceding DELETE and no transaction wrapper: one
  // statement needs neither (see findByCredential()). Unlike seedAccount(),
  // this must never remove an existing account.
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "INSERT INTO accounts (credential_hash, created_at) VALUES (?, ?);";
  if (sqlite3_prepare_v2(conn_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare account insert");
  }
  auto createdAt = nowIso8601();
  sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, createdAt.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error("failed to insert account");
  }
  sqlite3_finalize(stmt);

  return credential;
}

std::vector<AccountSummary> AccountsRepository::listAccounts() {
  auto guard = conn_.lock();

  sqlite3_stmt *stmt = nullptr;
  const char *sql = "SELECT id, created_at FROM accounts ORDER BY id;";
  if (sqlite3_prepare_v2(conn_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare account list");
  }

  std::vector<AccountSummary> result;
  int rc;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    AccountSummary summary;
    summary.id = sqlite3_column_int64(stmt, 0);
    const auto *createdAt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    summary.createdAt = createdAt ? createdAt : "";
    result.push_back(std::move(summary));
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to list accounts");
  }
  return result;
}

RevokeResult AccountsRepository::revokeAccount(AccountId id) {
  auto guard = conn_.lock();

  sqlite3_stmt *stmt = nullptr;
  const char *sql = "DELETE FROM accounts WHERE id = ?;";
  if (sqlite3_prepare_v2(conn_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare account delete");
  }
  sqlite3_bind_int64(stmt, 1, id);
  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc == SQLITE_CONSTRAINT) {
    return RevokeResult::InUse;
  }
  if (rc != SQLITE_DONE) {
    throw std::runtime_error("failed to delete account");
  }
  return sqlite3_changes(conn_.raw()) > 0 ? RevokeResult::Removed : RevokeResult::NotFound;
}

std::optional<AccountId> AccountsRepository::findByCredential(const std::string &rawCredential) {
  auto guard = conn_.lock();

  auto hash = fastHash(rawCredential);

  sqlite3_stmt *stmt = nullptr;
  const char *sql = "SELECT id FROM accounts WHERE credential_hash = ?;";
  if (sqlite3_prepare_v2(conn_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare account lookup");
  }
  sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);

  std::optional<AccountId> result;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    result = sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return result;
}

} // namespace pcm::tokenbackend
