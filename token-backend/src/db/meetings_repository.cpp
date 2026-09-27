#include "db/meetings_repository.h"
#include "crypto/random_token.h"

#include <chrono>
#include <cstdio>
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

Meeting readRow(sqlite3_stmt *stmt) {
  Meeting m;
  m.id = sqlite3_column_int64(stmt, 0);
  m.meetingRef = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  m.accountId = sqlite3_column_int64(stmt, 2);
  m.roomName = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
  m.scheduledStart = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
  m.scheduledEnd = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
  m.status = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
  return m;
}

constexpr const char *kSelectColumns =
    "id, meeting_ref, account_id, room_name, scheduled_start, scheduled_end, status";
} // namespace

Meeting MeetingsRepository::create(AccountId accountId, const std::string &scheduledStart,
                                    const std::string &scheduledEnd) {
  // Held for the whole body. Without it this INSERT can execute while another
  // thread's transaction is open, join that transaction, and be undone by its
  // rollback after this caller has already been handed the new meeting. The
  // findByRef() read-back below re-enters the lock on this same thread, which
  // is why it is recursive. See SqliteConnection::lock().
  auto guard = conn_.lock();

  std::string meetingRef = "mtg_" + generateUrlSafeToken(9);
  std::string roomName = "rm_" + generateUrlSafeToken(9);
  std::string createdAt = nowIso8601();

  sqlite3_stmt *stmt = nullptr;
  const char *sql = "INSERT INTO meetings "
                     "(meeting_ref, account_id, room_name, scheduled_start, scheduled_end, "
                     "status, created_at) VALUES (?, ?, ?, ?, ?, 'active', ?);";
  if (sqlite3_prepare_v2(conn_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare meeting insert");
  }
  sqlite3_bind_text(stmt, 1, meetingRef.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 2, accountId);
  sqlite3_bind_text(stmt, 3, roomName.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, scheduledStart.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, scheduledEnd.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, createdAt.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error("failed to insert meeting");
  }
  sqlite3_finalize(stmt);

  auto found = findByRef(meetingRef);
  if (!found) {
    throw std::runtime_error("meeting insert succeeded but readback failed");
  }
  return *found;
}

std::optional<Meeting> MeetingsRepository::findByRef(const std::string &meetingRef) {
  auto guard = conn_.lock();

  sqlite3_stmt *stmt = nullptr;
  std::string sql = std::string("SELECT ") + kSelectColumns + " FROM meetings WHERE meeting_ref = ?;";
  if (sqlite3_prepare_v2(conn_.raw(), sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare meeting lookup");
  }
  sqlite3_bind_text(stmt, 1, meetingRef.c_str(), -1, SQLITE_TRANSIENT);

  std::optional<Meeting> result;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    result = readRow(stmt);
  }
  sqlite3_finalize(stmt);
  return result;
}

std::optional<Meeting> MeetingsRepository::findById(int64_t meetingId) {
  auto guard = conn_.lock();

  sqlite3_stmt *stmt = nullptr;
  std::string sql = std::string("SELECT ") + kSelectColumns + " FROM meetings WHERE id = ?;";
  if (sqlite3_prepare_v2(conn_.raw(), sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare meeting lookup by id");
  }
  sqlite3_bind_int64(stmt, 1, meetingId);

  std::optional<Meeting> result;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    result = readRow(stmt);
  }
  sqlite3_finalize(stmt);
  return result;
}

void MeetingsRepository::invalidate(int64_t meetingId) {
  auto guard = conn_.lock();

  sqlite3_stmt *stmt = nullptr;
  const char *sql = "UPDATE meetings SET status = 'invalidated' WHERE id = ?;";
  if (sqlite3_prepare_v2(conn_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare meeting invalidate");
  }
  sqlite3_bind_int64(stmt, 1, meetingId);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error("failed to invalidate meeting");
  }
  sqlite3_finalize(stmt);
}

} // namespace pcm::tokenbackend
