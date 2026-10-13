#include "db/series_repository.h"
#include <stdexcept>
namespace pcm::tokenbackend {
namespace {
class Statement {
public:
  Statement(SqliteConnection &conn, const char *sql) {
    if (sqlite3_prepare_v2(conn.raw(), sql, -1, &s_, nullptr) != SQLITE_OK)
      throw std::runtime_error("series statement preparation failed");
  }
  ~Statement() { sqlite3_finalize(s_); }
  void text(int i, const std::string &s) { sqlite3_bind_text(s_, i, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT); }
  void number(int i, int64_t n) { sqlite3_bind_int64(s_, i, n); }
  bool row() {
    int rc = sqlite3_step(s_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    throw std::runtime_error("series statement execution failed");
  }
  void execute() { if (row()) throw std::runtime_error("unexpected series result"); }
  std::string text(int i) { return reinterpret_cast<const char *>(sqlite3_column_text(s_, i)); }
  int64_t number(int i) { return sqlite3_column_int64(s_, i); }
private:
  sqlite3_stmt *s_ = nullptr;
};
SeriesInvitation read(Statement &s) { return {s.number(0), s.text(1), s.number(2), s.number(3), s.text(4), static_cast<int>(s.number(5)), s.number(6) != 0}; }
}
std::optional<SeriesInvitation> SeriesRepository::byCodeHash(const std::string &hash) {
  auto lock = conn_.lock();
  Statement s(conn_, "SELECT i.id,i.series_uid,s.account_id,i.generation,i.passcode_hash,i.failed_attempts,i.revoked_at IS NOT NULL FROM series_invitations i JOIN schedule_series s USING(series_uid) WHERE i.code_hash=?");
  s.text(1, hash); if (s.row()) return read(s); return {};
}
std::optional<SeriesInvitation> SeriesRepository::current(const std::string &uid) {
  auto lock = conn_.lock();
  Statement s(conn_, "SELECT i.id,i.series_uid,s.account_id,i.generation,i.passcode_hash,i.failed_attempts,i.revoked_at IS NOT NULL FROM series_invitations i JOIN schedule_series s USING(series_uid) WHERE i.series_uid=? ORDER BY i.generation DESC LIMIT 1");
  s.text(1, uid); if (s.row()) return read(s); return {};
}
void SeriesRepository::insert(const std::string &uid, int64_t generation, const std::string &hash, const std::string &passcodeHash) {
  auto lock = conn_.lock(); Statement s(conn_, "INSERT INTO series_invitations(series_uid,generation,code_hash,passcode_hash) VALUES(?,?,?,?)");
  s.text(1, uid); s.number(2, generation); s.text(3, hash); s.text(4, passcodeHash); s.execute();
}
void SeriesRepository::revoke(const std::string &uid, int64_t now) {
  auto lock = conn_.lock(); Statement s(conn_, "UPDATE series_invitations SET revoked_at=? WHERE series_uid=? AND revoked_at IS NULL");
  s.number(1, now); s.text(2, uid); s.execute();
}
void SeriesRepository::fail(int64_t id, int64_t now) {
  auto lock = conn_.lock(); Statement s(conn_, "UPDATE series_invitations SET failed_attempts=failed_attempts+1, revoked_at=CASE WHEN failed_attempts>=5 THEN ? ELSE NULL END WHERE id=? AND revoked_at IS NULL");
  s.number(1, now); s.number(2, id); s.execute();
}
std::optional<InvitationReplay> SeriesRepository::replay(AccountId account, const std::string &key) {
  auto lock = conn_.lock(); Statement s(conn_, "SELECT series_uid,reissue,generation,encrypted_payload,expires_at FROM series_invitation_replays WHERE account_id=? AND idempotency_key=?");
  s.number(1, account); s.text(2, key);
  if (s.row()) return InvitationReplay{s.text(0), s.number(1) != 0, s.number(2), s.text(3), s.number(4)};
  return {};
}
void SeriesRepository::saveReplay(AccountId account, const std::string &key, const InvitationReplay &r) {
  auto lock = conn_.lock(); Statement s(conn_, "INSERT INTO series_invitation_replays VALUES(?,?,?,?,?,?,?)");
  s.number(1, account); s.text(2, key); s.text(3, r.uid); s.number(4, r.reissue); s.number(5, r.generation); s.text(6, r.encryptedPayload); s.number(7, r.expiresAt); s.execute();
}
void SeriesRepository::retireReplays(int64_t now) {
  auto lock = conn_.lock(); Statement s(conn_, "UPDATE series_invitation_replays SET encrypted_payload='' WHERE expires_at<=? AND encrypted_payload<>''");
  s.number(1, now); s.execute();
}
std::optional<int64_t> SeriesRepository::meetingId(const std::string &uid, int64_t originalStartMs) {
  auto lock = conn_.lock(); Statement s(conn_, "SELECT meeting_id FROM occurrence_meetings WHERE series_uid=? AND original_start_ms=?");
  s.text(1, uid); s.number(2, originalStartMs); if (s.row()) return s.number(0); return {};
}
std::optional<OccurrenceMapping> SeriesRepository::mapping(int64_t meetingId) {
  auto lock = conn_.lock(); Statement s(conn_, "SELECT series_uid,original_start_ms FROM occurrence_meetings WHERE meeting_id=?");
  s.number(1, meetingId); if (s.row()) return OccurrenceMapping{s.text(0), s.number(1)}; return {};
}
void SeriesRepository::map(const std::string &uid, int64_t originalStartMs, int64_t meetingId) {
  auto lock = conn_.lock(); Statement s(conn_, "INSERT INTO occurrence_meetings VALUES(?,?,?)");
  s.text(1, uid); s.number(2, originalStartMs); s.number(3, meetingId); s.execute();
}
}
