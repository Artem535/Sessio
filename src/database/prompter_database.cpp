#include "database.h"

namespace pcm::database {
namespace {
auto query(duckdb::Connection &conn, const std::string &sql, duckdb::vector<duckdb::Value> values) {
  auto prepared = conn.Prepare(sql);
  if (!prepared || prepared->HasError()) throw std::runtime_error("Prompter database operation failed");
  auto result = prepared->Execute(values);
  if (!result || result->HasError()) throw std::runtime_error("Prompter database operation failed");
  return result;
}
class Transaction {
public:
  explicit Transaction(duckdb::Connection &conn) : mConn(conn) {
    if (conn.Query("BEGIN TRANSACTION")->HasError()) throw std::runtime_error("Cannot start prompter transaction");
  }
  ~Transaction() { if (!mCommitted) mConn.Query("ROLLBACK"); }
  bool commit() { mCommitted = !mConn.Query("COMMIT")->HasError(); return mCommitted; }
private:
  duckdb::Connection &mConn;
  bool mCommitted = false;
};
}

void Database::init_prompter_schema() {
  duckdb::Connection conn(*mDb);
  Transaction tx(conn);
  auto result = conn.Query(
      "CREATE TABLE IF NOT EXISTS TranscriptPrompterState ("
      "transcript_id INTEGER PRIMARY KEY, revision BIGINT NOT NULL, state_json VARCHAR NOT NULL);"
      "CREATE TABLE IF NOT EXISTS TranscriptPrompterNote ("
      "transcript_id INTEGER NOT NULL, card_id VARCHAR NOT NULL, client_id INTEGER NOT NULL,"
      "note_id INTEGER NOT NULL, PRIMARY KEY(transcript_id, card_id));"
      "UPDATE ApplicationMetadata SET schema_version=3,last_migration_at=current_timestamp "
      "WHERE id=1 AND schema_version IN (1,2);");
  if (!result || result->HasError() || !tx.commit()) throw std::runtime_error("Cannot migrate prompter schema");
}

PrompterState Database::get_prompter_state(int64_t transcript_id) {
  duckdb::Connection conn(*mDb);
  auto result = query(conn, "SELECT revision,state_json FROM TranscriptPrompterState WHERE transcript_id=$1",
                      {duckdb::Value::BIGINT(transcript_id)});
  auto chunk = result->Fetch();
  if (!chunk || !chunk->size()) return {};
  return {chunk->GetValue(0, 0).GetValue<int64_t>(), chunk->GetValue(1, 0).ToString()};
}

bool Database::save_prompter_state(int64_t transcript_id, int64_t expected_revision, const std::string &json) {
  if (transcript_id <= 0 || expected_revision < 0 || json.empty() || json.size() > 1024 * 1024) return false;
  try {
    duckdb::Connection conn(*mDb);
    Transaction tx(conn);
    auto exists = query(conn, "SELECT id FROM Transcript WHERE id=$1", {duckdb::Value::BIGINT(transcript_id)})->Fetch();
    if (!exists || !exists->size()) return false;
    auto result = expected_revision == 0
        ? query(conn, "INSERT INTO TranscriptPrompterState VALUES($1,1,$2) ON CONFLICT DO NOTHING RETURNING revision",
                {duckdb::Value::BIGINT(transcript_id), duckdb::Value(json)})
        : query(conn, "UPDATE TranscriptPrompterState SET revision=revision+1,state_json=$2 "
                      "WHERE transcript_id=$1 AND revision=$3 RETURNING revision",
                {duckdb::Value::BIGINT(transcript_id), duckdb::Value(json), duckdb::Value::BIGINT(expected_revision)});
    auto chunk = result->Fetch();
    return chunk && chunk->size() && tx.commit();
  } catch (...) { return false; }
}

int64_t Database::save_prompter_note(int64_t transcript_id, const std::string &card_id,
                                   int64_t client_id, const std::string &body) {
  if (transcript_id <= 0 || client_id <= 0 || card_id.empty() || card_id.size() > 80 ||
      body.empty() || body.size() > 16000) return 0;
  try {
    duckdb::Connection conn(*mDb);
    Transaction tx(conn);
    auto linked = query(conn, "SELECT t.event_id FROM Transcript t JOIN TranscriptClient tc "
                        "ON tc.transcript_id=t.id JOIN Client c ON c.id=tc.client_id "
                        "WHERE t.id=$1 AND tc.client_id=$2",
                        {duckdb::Value::BIGINT(transcript_id), duckdb::Value::BIGINT(client_id)})->Fetch();
    if (!linked || !linked->size()) return 0;
    auto existing = query(conn, "SELECT note_id,client_id FROM TranscriptPrompterNote WHERE transcript_id=$1 AND card_id=$2",
                          {duckdb::Value::BIGINT(transcript_id), duckdb::Value(card_id)})->Fetch();
    if (existing && existing->size())
      return existing->GetValue(1, 0).GetValue<int32_t>() == client_id
                 ? existing->GetValue(0, 0).GetValue<int32_t>() : 0;
    const auto time = db_utils::toDuckTimestamp(Poco::Timestamp().epochMicroseconds());
    auto inserted = query(conn, constance::kInsertClientNoteQuery,
                          {duckdb::Value::BIGINT(client_id), duckdb::Value(body), time, time,
                           linked->GetValue(0, 0), duckdb::Value(), duckdb::Value()})->Fetch();
    if (!inserted || !inserted->size()) return 0;
    const auto note_id = inserted->GetValue(0, 0).GetValue<int32_t>();
    query(conn, "INSERT INTO TranscriptPrompterNote VALUES($1,$2,$3,$4)",
          {duckdb::Value::BIGINT(transcript_id), duckdb::Value(card_id),
           duckdb::Value::BIGINT(client_id), duckdb::Value::BIGINT(note_id)});
    return tx.commit() ? note_id : 0;
  } catch (...) { return 0; }
}
} // namespace pcm::database
