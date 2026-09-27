#include "db/invitations_repository.h"
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

constexpr const char *kSelectColumns =
    "id, meeting_id, account_id, passcode_hash, passcode_attempts, status";
constexpr int kMaxPasscodeAttempts = 5;

Invitation readRow(sqlite3_stmt *stmt) {
  Invitation inv;
  inv.id = sqlite3_column_int64(stmt, 0);
  inv.meetingId = sqlite3_column_int64(stmt, 1);
  inv.accountId = sqlite3_column_int64(stmt, 2);
  inv.passcodeHash = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
  inv.passcodeAttempts = sqlite3_column_int(stmt, 4);
  inv.status = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
  return inv;
}
} // namespace

InvitationsRepository::CreateResult InvitationsRepository::create(int64_t meetingId,
                                                                    AccountId accountId) {
  // Held for the whole body. Without it this INSERT can execute while another
  // thread's transaction is open, join that transaction, and be undone by its
  // rollback after this caller has already been handed the invitation code.
  // reissueForMeeting() also calls this with the lock already held on this
  // thread, which is why it is recursive. See SqliteConnection::lock().
  auto guard = conn_.lock();

  std::string invitationCode = generateUrlSafeToken(24);
  std::string passcode = generateNumericPasscode();
  std::string invitationCodeHash = fastHash(invitationCode);
  std::string passcodeHash = hashPasscode(passcode);
  std::string createdAt = nowIso8601();

  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "INSERT INTO invitations "
      "(meeting_id, account_id, invitation_code_hash, passcode_hash, passcode_attempts, "
      "status, created_at) VALUES (?, ?, ?, ?, 0, 'active', ?);";
  if (sqlite3_prepare_v2(conn_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare invitation insert");
  }
  sqlite3_bind_int64(stmt, 1, meetingId);
  sqlite3_bind_int64(stmt, 2, accountId);
  sqlite3_bind_text(stmt, 3, invitationCodeHash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, passcodeHash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, createdAt.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error("failed to insert invitation");
  }
  sqlite3_finalize(stmt);

  auto found = findByCode(invitationCode);
  if (!found) {
    throw std::runtime_error("invitation insert succeeded but readback failed");
  }
  return CreateResult{invitationCode, passcode, *found};
}

std::optional<Invitation> InvitationsRepository::findByCode(const std::string &invitationCode) {
  auto guard = conn_.lock();

  auto hash = fastHash(invitationCode);

  sqlite3_stmt *stmt = nullptr;
  std::string sql =
      std::string("SELECT ") + kSelectColumns + " FROM invitations WHERE invitation_code_hash = ?;";
  if (sqlite3_prepare_v2(conn_.raw(), sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare invitation lookup");
  }
  sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);

  std::optional<Invitation> result;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    result = readRow(stmt);
  }
  sqlite3_finalize(stmt);
  return result;
}

int InvitationsRepository::recordFailedPasscodeAttempt(int64_t invitationId) {
  // This implements ADR-12's actual security boundary: 6 digits are trivially
  // brute-forceable, and only the hard 5-attempt cap makes them safe. The
  // UPDATE, the read-back and the auto-invalidate must therefore be one atomic
  // step. Serialized threading mode alone does not give that — it makes each
  // statement safe in isolation, so two concurrent guesses on the same
  // invitation can interleave, both read the same post-increment count, and
  // let an attacker run past the cap.
  //
  // The lock is what actually delivers that atomicity, and it has to be taken
  // before the transaction: the whole point is that no other thread can issue
  // a statement — or a second BEGIN IMMEDIATE, which would fail outright with
  // SQLITE_ERROR — between this BEGIN and its COMMIT. This is the hottest path
  // for real concurrency, since POST /v1/invitations/{code}/client-token is
  // unauthenticated. The invalidate() call at the bottom re-enters the lock on
  // this same thread.
  auto guard = conn_.lock();
  SqliteTransaction tx(conn_);

  sqlite3_stmt *stmt = nullptr;
  const char *sql = "UPDATE invitations SET passcode_attempts = passcode_attempts + 1 "
                     "WHERE id = ?;";
  if (sqlite3_prepare_v2(conn_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare attempt increment");
  }
  sqlite3_bind_int64(stmt, 1, invitationId);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error("failed to increment passcode attempts");
  }
  sqlite3_finalize(stmt);

  sqlite3_stmt *readStmt = nullptr;
  sqlite3_prepare_v2(conn_.raw(), "SELECT passcode_attempts FROM invitations WHERE id = ?;", -1,
                      &readStmt, nullptr);
  sqlite3_bind_int64(readStmt, 1, invitationId);
  int attempts = 0;
  if (sqlite3_step(readStmt) == SQLITE_ROW) {
    attempts = sqlite3_column_int(readStmt, 0);
  }
  sqlite3_finalize(readStmt);

  if (attempts >= kMaxPasscodeAttempts) {
    invalidate(invitationId);
  }

  tx.commit();
  return attempts;
}

void InvitationsRepository::invalidateAllForMeeting(int64_t meetingId) {
  auto guard = conn_.lock();

  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "UPDATE invitations SET status = 'invalidated' WHERE meeting_id = ? AND status = 'active';";
  if (sqlite3_prepare_v2(conn_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare meeting-wide invitation invalidate");
  }
  sqlite3_bind_int64(stmt, 1, meetingId);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error("failed to invalidate invitations for meeting");
  }
  sqlite3_finalize(stmt);
}

InvitationsRepository::CreateResult
InvitationsRepository::reissueForMeeting(int64_t meetingId, AccountId accountId) {
  // Before the transaction, for the same reason as recordFailedPasscodeAttempt:
  // the lock is what keeps another thread's statement out of this BEGIN/COMMIT
  // and stops a concurrent BEGIN IMMEDIATE from failing with SQLITE_ERROR. The
  // two calls below re-enter it on this thread.
  auto guard = conn_.lock();
  SqliteTransaction tx(conn_);
  invalidateAllForMeeting(meetingId);
  auto created = create(meetingId, accountId);
  tx.commit();
  return created;
}

void InvitationsRepository::invalidate(int64_t invitationId) {
  auto guard = conn_.lock();

  sqlite3_stmt *stmt = nullptr;
  const char *sql = "UPDATE invitations SET status = 'invalidated' WHERE id = ?;";
  if (sqlite3_prepare_v2(conn_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare invitation invalidate");
  }
  sqlite3_bind_int64(stmt, 1, invitationId);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error("failed to invalidate invitation");
  }
  sqlite3_finalize(stmt);
}

} // namespace pcm::tokenbackend
