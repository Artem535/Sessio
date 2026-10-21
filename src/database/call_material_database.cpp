#include "database.h"

#include <limits>
#include <rfl/Generic.hpp>
#include <rfl/json.hpp>

namespace pcm::database {
namespace {

auto query(duckdb::Connection &conn, const std::string &sql,
           duckdb::vector<duckdb::Value> values = {}) {
  auto statement = conn.Prepare(sql);
  if (!statement || statement->HasError()) throw std::runtime_error("Call material query failed");
  auto result = statement->Execute(values);
  if (!result || result->HasError()) throw std::runtime_error("Call material query failed");
  return result;
}

class Transaction {
 public:
  explicit Transaction(duckdb::Connection &conn) : conn_(conn) {
    query(conn_, "BEGIN TRANSACTION");
  }
  ~Transaction() { if (!committed_) conn_.Query("ROLLBACK"); }
  bool commit() {
    auto result = conn_.Query("COMMIT");
    committed_ = result && !result->HasError();
    return committed_;
  }
 private:
  duckdb::Connection &conn_;
  bool committed_ = false;
};

CallMaterialState readMaterial(const duckdb::DataChunk &chunk, duckdb::idx_t row) {
  CallMaterialState state;
  state.id = chunk.GetValue(0, row).GetValue<int64_t>();
  state.revision = chunk.GetValue(1, row).GetValue<int64_t>();
  if (!chunk.GetValue(2, row).IsNull()) state.event_id = chunk.GetValue(2, row).GetValue<int64_t>();
  state.json = chunk.GetValue(3, row).ToString();
  state.created_at = chunk.GetValue(4, row).GetValue<int64_t>();
  return state;
}

bool manualField(const std::string &key) {
  return key == "manual_notes" || key == "manual_text" || key == "manual_entries" ||
         key == "manual_draft_attachments";
}

std::optional<rfl::Generic::Object> parseMaterial(const std::string &json) {
  if (json.empty() || json.size() > 4 * 1024 * 1024) return std::nullopt;
  auto parsed = rfl::json::read<rfl::Generic>(json);
  if (!parsed) return std::nullopt;
  auto object = parsed->to_object();
  if (!object) return std::nullopt;
  for (const auto &[key, value] : *object) {
    if (key == "manual_entries" || key == "manual_draft_attachments") {
      if (!value.to_array()) return std::nullopt;
    } else if (manualField(key) && !value.to_string()) return std::nullopt;
  }
  return *object;
}

}  // namespace

void Database::init_call_material_schema() {
  duckdb::Connection conn(*mDb);
  Transaction tx(conn);
  // Explicit cascades avoid DuckDB foreign-key limitations on same-transaction
  // parent/child updates. Sequences avoid reusing a deleted material's identity.
  auto result = conn.Query(
      "CREATE SEQUENCE IF NOT EXISTS call_material_id_seq START 1;"
      "CREATE TABLE IF NOT EXISTS CallMaterial ("
      "id BIGINT PRIMARY KEY DEFAULT nextval('call_material_id_seq'),"
      "revision BIGINT NOT NULL DEFAULT 0 CHECK(revision>=0), event_id BIGINT,"
      "state_json VARCHAR NOT NULL DEFAULT '{}', created_at BIGINT NOT NULL);"
      "CREATE TABLE IF NOT EXISTS CallMaterialTranscript ("
      "material_id BIGINT NOT NULL, transcript_id INTEGER PRIMARY KEY);");
  if (!result || result->HasError())
    throw std::runtime_error("Cannot migrate call materials to schema 4");
  // Validate existing table shapes before advancing metadata. DDL and metadata
  // stay in the same transaction if an interrupted/invalid schema is present.
  query(conn, "SELECT id,revision,event_id,state_json,created_at FROM CallMaterial LIMIT 0");
  query(conn, "SELECT material_id,transcript_id FROM CallMaterialTranscript LIMIT 0");
  query(conn, "UPDATE ApplicationMetadata SET schema_version=4,last_migration_at=current_timestamp "
              "WHERE id=1 AND schema_version=3");
  if (!tx.commit()) throw std::runtime_error("Cannot commit call material migration");
}

int64_t Database::create_call_material(std::optional<int64_t> event_id) {
  if (event_id && *event_id <= 0) return 0;
  try {
    duckdb::Connection conn(*mDb);
    Transaction tx(conn);
    if (event_id) {
      auto found = query(conn, "SELECT id FROM Event WHERE id=$1", {duckdb::Value::BIGINT(*event_id)})->Fetch();
      if (!found || !found->size()) return 0;
    }
    auto chunk = query(conn, "INSERT INTO CallMaterial(event_id,created_at) VALUES ($1,$2) RETURNING id",
                       {db_utils::toDuckValue(event_id),
                        duckdb::Value::BIGINT(Poco::Timestamp().epochMicroseconds() / 1000)})->Fetch();
    if (!chunk || !chunk->size()) return 0;
    const auto id = chunk->GetValue(0, 0).GetValue<int64_t>();
    return tx.commit() ? id : 0;
  } catch (const std::exception &) { return 0; }
}

bool Database::link_call_material_transcript(int64_t material_id, int64_t transcript_id) {
  if (material_id <= 0 || transcript_id <= 0) return false;
  try {
    duckdb::Connection conn(*mDb);
    Transaction tx(conn);
    auto exists = query(conn, "SELECT m.event_id,t.event_id FROM CallMaterial m, Transcript t WHERE m.id=$1 AND t.id=$2 "
                             "AND t.consent_revoked_at IS NULL",
                        {duckdb::Value::BIGINT(material_id), duckdb::Value::BIGINT(transcript_id)})->Fetch();
    if (!exists || !exists->size()) return false;
    const auto materialEvent = exists->GetValue(0, 0);
    const auto transcriptEvent = exists->GetValue(1, 0);
    if (!materialEvent.IsNull() && !transcriptEvent.IsNull() &&
        materialEvent.GetValue<int64_t>() != transcriptEvent.GetValue<int64_t>()) return false;
    auto linked = query(conn, "SELECT material_id FROM CallMaterialTranscript WHERE transcript_id=$1",
                        {duckdb::Value::BIGINT(transcript_id)})->Fetch();
    if (linked && linked->size() && linked->GetValue(0, 0).GetValue<int64_t>() != material_id) return false;
    // The scheduled occurrence may only have been materialized after the call
    // began. Repeating an existing link also refreshes this late association.
    if (materialEvent.IsNull() && !transcriptEvent.IsNull())
      query(conn, "UPDATE CallMaterial SET event_id=$2 WHERE id=$1",
            {duckdb::Value::BIGINT(material_id), transcriptEvent});
    if (linked && linked->size()) return tx.commit();
    query(conn, "INSERT INTO CallMaterialTranscript VALUES ($1,$2)",
          {duckdb::Value::BIGINT(material_id), duckdb::Value::BIGINT(transcript_id)});
    if (!invalidate_call_materials(conn, transcript_id)) return false;
    return tx.commit();
  } catch (const std::exception &) { return false; }
}

CallMaterialState Database::get_call_material(int64_t material_id) {
  duckdb::Connection conn(*mDb);
  auto chunk = query(conn, "SELECT id,revision,event_id,state_json,created_at FROM CallMaterial WHERE id=$1",
                     {duckdb::Value::BIGINT(material_id)})->Fetch();
  return chunk && chunk->size() ? readMaterial(*chunk, 0) : CallMaterialState{};
}

CallMaterialState Database::get_call_material_for_transcript(int64_t transcript_id) {
  duckdb::Connection conn(*mDb);
  auto chunk = query(conn, "SELECT m.id,m.revision,m.event_id,m.state_json,m.created_at FROM CallMaterial m "
                           "JOIN CallMaterialTranscript t ON t.material_id=m.id WHERE t.transcript_id=$1",
                     {duckdb::Value::BIGINT(transcript_id)})->Fetch();
  return chunk && chunk->size() ? readMaterial(*chunk, 0) : CallMaterialState{};
}

std::vector<CallMaterialState> Database::get_call_materials() {
  duckdb::Connection conn(*mDb);
  auto result = query(conn, "SELECT id,revision,event_id,state_json,created_at FROM CallMaterial "
                            "ORDER BY created_at DESC,id DESC");
  std::vector<CallMaterialState> states;
  while (auto chunk = result->Fetch())
    for (duckdb::idx_t row = 0; row < chunk->size(); ++row) states.push_back(readMaterial(*chunk, row));
  return states;
}

std::vector<int64_t> Database::get_call_material_transcript_ids(int64_t material_id) {
  duckdb::Connection conn(*mDb);
  auto result = query(conn, "SELECT transcript_id FROM CallMaterialTranscript WHERE material_id=$1 "
                            "ORDER BY transcript_id", {duckdb::Value::BIGINT(material_id)});
  std::vector<int64_t> ids;
  while (auto chunk = result->Fetch())
    for (duckdb::idx_t row = 0; row < chunk->size(); ++row)
      ids.push_back(chunk->GetValue(0, row).GetValue<int64_t>());
  return ids;
}

bool Database::save_call_material(int64_t material_id, int64_t expected_revision, const std::string &json) {
  if (material_id <= 0 || expected_revision < 0 ||
      expected_revision == std::numeric_limits<int64_t>::max() || !parseMaterial(json)) return false;
  try {
    duckdb::Connection conn(*mDb);
    auto chunk = query(conn, "UPDATE CallMaterial SET state_json=$3,revision=revision+1 "
                             "WHERE id=$1 AND revision=$2 RETURNING id",
                       {duckdb::Value::BIGINT(material_id), duckdb::Value::BIGINT(expected_revision),
                        duckdb::Value(json)})->Fetch();
    return chunk && chunk->size();
  } catch (const std::exception &) { return false; }
}

bool Database::invalidate_call_materials(duckdb::Connection &conn, int64_t transcript_id) {
  try {
    auto chunk = query(conn, "SELECT m.id,m.state_json FROM CallMaterial m JOIN CallMaterialTranscript t "
                             "ON t.material_id=m.id WHERE t.transcript_id=$1",
                       {duckdb::Value::BIGINT(transcript_id)})->Fetch();
    if (!chunk || !chunk->size()) return true;
    auto parsed = parseMaterial(chunk->GetValue(1, 0).ToString());
    if (!parsed) return false;  // Do not silently destroy unreadable manual notes.
    rfl::Generic::Object manual;
    for (const auto &[key, value] : *parsed) {
      if (manualField(key) || key == "ended") manual.insert(key, value);
      else if (key == "live_status" || key == "final_status") {
        const auto status = value.to_string();
        // Historical null/empty values mean no processing has started. Linking
        // a first transcript must not invent an obsolete generated result.
        if (status && !status->empty())
          manual.insert(key, rfl::Generic(std::string(*status == "idle" ? "idle" : "stale")));
      }
    }
    query(conn, "UPDATE CallMaterial SET state_json=$2,revision=revision+1 WHERE id=$1",
          {chunk->GetValue(0, 0), duckdb::Value(rfl::json::write(manual))});
    return true;
  } catch (const std::exception &) { return false; }
}

}  // namespace pcm::database
