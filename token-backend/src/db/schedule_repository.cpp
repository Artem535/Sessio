#include "db/schedule_repository.h"
#include <algorithm>
#include <cctype>
#include <sodium.h>
#include <stdexcept>

namespace pcm::tokenbackend {
namespace {
class Statement {
public:
  Statement(SqliteConnection &conn, const char *sql) {
    if (sqlite3_prepare_v2(conn.raw(), sql, -1, &stmt_, nullptr) != SQLITE_OK)
      throw std::runtime_error("schedule statement preparation failed");
  }
  ~Statement() { sqlite3_finalize(stmt_); }
  void text(int i, const std::string &s) { sqlite3_bind_text(stmt_, i, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT); }
  void number(int i, int64_t n) { sqlite3_bind_int64(stmt_, i, n); }
  void null(int i) { sqlite3_bind_null(stmt_, i); }
  bool row() {
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    throw std::runtime_error("schedule statement execution failed");
  }
  void execute() { if (row()) throw std::runtime_error("unexpected schedule result"); }
  std::string text(int i) const { return reinterpret_cast<const char *>(sqlite3_column_text(stmt_, i)); }
  int64_t number(int i) const { return sqlite3_column_int64(stmt_, i); }
  bool isNull(int i) const { return sqlite3_column_type(stmt_, i) == SQLITE_NULL; }
private:
  sqlite3_stmt *stmt_ = nullptr;
};

std::string contentHash(const schedule::Snapshot &s) {
  // Fixed length-prefixed fields are unambiguous even with embedded delimiters.
  // Revision is transport state and deliberately excluded from this SHA-256.
  std::string canonical = "pcm-schedule-v1:";
  auto field = [&](const std::string &v) { canonical += std::to_string(v.size()) + ":" + v; };
  field(s.timezone); field(s.dtstartLocal); field(std::to_string(s.durationSeconds));
  field(s.rrule); field(s.untilMs ? std::to_string(*s.untilMs) : "null");
  field(s.active ? "true" : "false"); field(s.joinEnabled ? "true" : "false");
  field(std::to_string(s.overrides.size()));
  for (const auto &o : s.overrides) {
    field(std::to_string(o.originalStartMs)); field(std::to_string(o.startMs));
    field(std::to_string(o.endMs)); field(o.joinEnabled ? "true" : "false");
  }
  field(std::to_string(s.exceptions.size()));
  for (auto e : s.exceptions) field(std::to_string(e));
  unsigned char digest[crypto_hash_sha256_BYTES];
  crypto_hash_sha256(digest, reinterpret_cast<const unsigned char *>(canonical.data()), canonical.size());
  char hex[crypto_hash_sha256_BYTES * 2 + 1];
  sodium_bin2hex(hex, sizeof(hex), digest, sizeof(digest));
  return hex;
}
} // namespace

std::optional<std::string> normalizeSeriesUid(const std::string &uid) {
  if (uid.size() != 36) return {};
  auto normalized = uid;
  bool nonzero = false;
  for (size_t i = 0; i < uid.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) { if (uid[i] != '-') return {}; }
    else {
      auto c = static_cast<unsigned char>(uid[i]);
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return {};
      normalized[i] = static_cast<char>(std::tolower(c)); nonzero |= c != '0';
    }
  }
  return nonzero ? std::optional<std::string>(normalized) : std::nullopt;
}

std::optional<StoredSchedule> ScheduleRepository::get(AccountId accountId, const std::string &requestedUid) {
  auto guard = conn_.lock();
  auto normalized = normalizeSeriesUid(requestedUid);
  if (!normalized) return {};
  const auto &seriesUid = *normalized;
  Statement q(conn_, "SELECT revision,timezone,dtstart_local,duration_seconds,rrule,until_ms,active,join_enabled,content_hash FROM schedule_series WHERE series_uid=? AND account_id=?");
  q.text(1, seriesUid); q.number(2, accountId);
  if (!q.row()) return {};
  StoredSchedule stored{seriesUid, accountId, {}, q.text(8)};
  auto &s = stored.snapshot;
  s.revision = q.number(0); s.baseRevision = s.revision - 1;
  s.timezone = q.text(1); s.dtstartLocal = q.text(2); s.durationSeconds = q.number(3);
  s.rrule = q.text(4); if (!q.isNull(5)) s.untilMs = q.number(5);
  s.active = q.number(6); s.joinEnabled = q.number(7);
  Statement overrides(conn_, "SELECT original_start_ms,start_ms,end_ms,join_enabled FROM schedule_overrides WHERE series_uid=? ORDER BY original_start_ms");
  overrides.text(1, seriesUid);
  while (overrides.row()) s.overrides.push_back({overrides.number(0), overrides.number(1), overrides.number(2), overrides.number(3) != 0});
  Statement exceptions(conn_, "SELECT original_start_ms FROM schedule_exceptions WHERE series_uid=? ORDER BY original_start_ms");
  exceptions.text(1, seriesUid);
  while (exceptions.row()) s.exceptions.push_back(exceptions.number(0));
  return stored;
}

ScheduleWriteResult ScheduleRepository::put(AccountId accountId, const std::string &requestedUid,
                                           const schedule::Snapshot &snapshot) {
  auto guard = conn_.lock();
  auto normalized = normalizeSeriesUid(requestedUid);
  if (!normalized) return {ScheduleWriteStatus::Invalid, {}};
  const auto &seriesUid = *normalized;
  if (!schedule::validate(snapshot).valid) return {ScheduleWriteStatus::Invalid, {}};
  auto s = snapshot;
  std::sort(s.overrides.begin(), s.overrides.end(), [](const auto &a, const auto &b) { return a.originalStartMs < b.originalStartMs; });
  std::sort(s.exceptions.begin(), s.exceptions.end());
  auto hash = contentHash(s);
  SqliteTransaction tx(conn_);
  Statement existing(conn_, "SELECT account_id,revision,content_hash FROM schedule_series WHERE series_uid=?");
  existing.text(1, seriesUid);
  if (existing.row()) {
    if (existing.number(0) != accountId) return {ScheduleWriteStatus::NotFound, {}};
    if (existing.number(1) == s.revision) {
      if (existing.text(2) != hash) return {ScheduleWriteStatus::Conflict, {}};
      auto stored = get(accountId, seriesUid);
      tx.commit();
      return {ScheduleWriteStatus::Applied, std::move(stored)};
    }
    if (existing.number(1) != s.baseRevision) return {ScheduleWriteStatus::Conflict, {}};
  } else if (s.baseRevision != 0) return {ScheduleWriteStatus::Conflict, {}};
  Statement insert(conn_, "INSERT INTO schedule_series(series_uid,account_id,revision,timezone,dtstart_local,duration_seconds,rrule,until_ms,active,join_enabled,content_hash) VALUES(?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(series_uid) DO UPDATE SET revision=excluded.revision,timezone=excluded.timezone,dtstart_local=excluded.dtstart_local,duration_seconds=excluded.duration_seconds,rrule=excluded.rrule,until_ms=excluded.until_ms,active=excluded.active,join_enabled=excluded.join_enabled,content_hash=excluded.content_hash");
  insert.text(1, seriesUid); insert.number(2, accountId); insert.number(3, s.revision);
  insert.text(4, s.timezone); insert.text(5, s.dtstartLocal); insert.number(6, s.durationSeconds);
  insert.text(7, s.rrule); if (s.untilMs) insert.number(8, *s.untilMs); else insert.null(8);
  insert.number(9, s.active); insert.number(10, s.joinEnabled); insert.text(11, hash);
  insert.execute();
  for (auto sql : {"DELETE FROM schedule_overrides WHERE series_uid=?", "DELETE FROM schedule_exceptions WHERE series_uid=?"}) {
    Statement erase(conn_, sql); erase.text(1, seriesUid); erase.execute();
  }
  for (const auto &o : s.overrides) {
    Statement child(conn_, "INSERT INTO schedule_overrides VALUES(?,?,?,?,?)");
    child.text(1, seriesUid); child.number(2, o.originalStartMs); child.number(3, o.startMs);
    child.number(4, o.endMs); child.number(5, o.joinEnabled); child.execute();
  }
  for (auto e : s.exceptions) {
    Statement child(conn_, "INSERT INTO schedule_exceptions VALUES(?,?)");
    child.text(1, seriesUid); child.number(2, e); child.execute();
  }
  tx.commit();
  return {ScheduleWriteStatus::Applied, StoredSchedule{seriesUid, accountId, std::move(s), hash}};
}
} // namespace pcm::tokenbackend
