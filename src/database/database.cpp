#include "database.h"
#include <set>

#include <cassert>
#include <thread>

#include <Poco/UUIDGenerator.h>

namespace pcm::database {
namespace {
duckdb::Value fkOrNull(const int64_t id) {
  return id > 0 ? duckdb::Value::INTEGER(static_cast<int32_t>(id))
                : duckdb::Value();
}

duckdb::Value timestampMsOrNull(const std::optional<int64_t> &ms_opt) {
  if (!ms_opt.has_value()) {
    return duckdb::Value();
  }

  return db_utils::toDuckTimestamp(*ms_opt * 1000);
}

int32_t statusOrDefault(const int64_t id) {
  return static_cast<int32_t>(id > 0 ? id : 1);
}

void appendProviderValues(duckdb::vector<duckdb::Value> &values,
                          const std::optional<std::string> &providerKind,
                          const std::optional<std::string> &meetingRef,
                          const std::optional<std::string> &invitationState) {
  values.push_back(db_utils::toDuckValue(providerKind));
  values.push_back(db_utils::toDuckValue(meetingRef));
  values.push_back(db_utils::toDuckValue(invitationState));
}

std::unique_ptr<duckdb::QueryResult> executePrepared(
    duckdb::Connection &conn, const std::string &query,
    duckdb::vector<duckdb::Value> values) {
  auto statement = conn.Prepare(query);
  if (!statement || statement->HasError()) {
    return nullptr;
  }
  auto boundValues = std::move(values);
  return statement->Execute(boundValues);
}

std::int64_t nowMs() { return Poco::Timestamp().epochMicroseconds() / 1000; }

duckdb::Value nowTimestamp() {
  return db_utils::toDuckTimestamp(std::make_optional(nowMs() * 1000));
}

// Explicit transaction on one connection; rolls back unless commit() ran.
class Transaction {
public:
  explicit Transaction(duckdb::Connection &conn) : mConn(conn) {
    auto result = mConn.Query("BEGIN TRANSACTION");
    mActive = result && !result->HasError();
  }
  ~Transaction() {
    if (mActive) {
      mConn.Query("ROLLBACK");
    }
  }
  Transaction(const Transaction &) = delete;
  Transaction &operator=(const Transaction &) = delete;
  [[nodiscard]] bool active() const { return mActive; }
  bool commit() {
    if (!mActive) {
      return false;
    }
    mActive = false;
    auto result = mConn.Query("COMMIT");
    if (!result || result->HasError()) {
      mConn.Query("ROLLBACK");
      return false;
    }
    return true;
  }

private:
  duckdb::Connection &mConn;
  bool mActive = false;
};

std::optional<ScheduleIdentity> identityFromChunk(const duckdb::DataChunk &chunk,
                                                  const duckdb::idx_t row) {
  ScheduleIdentity identity;
  identity.series_id = db_utils::toInt32AsInt64(chunk.GetValue(0, row));
  identity.series_uid = chunk.GetValue(1, row).ToString();
  identity.timezone = chunk.GetValue(2, row).ToString();
  identity.invitation_generation = chunk.GetValue(3, row).GetValue<int64_t>();
  identity.invitation_key = db_utils::toOptionalString(chunk.GetValue(4, row));
  identity.desired_revision = chunk.GetValue(5, row).GetValue<int64_t>();
  identity.acked_revision = chunk.GetValue(6, row).GetValue<int64_t>();
  identity.acked_content_hash =
      db_utils::toOptionalString(chunk.GetValue(7, row)).value_or("");
  identity.sync_state = chunk.GetValue(8, row).ToString();
  identity.last_error = db_utils::toOptionalString(chunk.GetValue(9, row)).value_or("");
  return identity;
}

std::optional<std::int64_t> optionalBigint(const duckdb::Value &value) {
  if (value.IsNull()) {
    return std::nullopt;
  }
  return value.GetValue<int64_t>();
}

// Rows touched by an UPDATE/DELETE, or nullopt on failure.
std::optional<std::int64_t> affectedRows(duckdb::QueryResult *result) {
  if (!result || result->HasError()) {
    return std::nullopt;
  }
  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    return 0;
  }
  return chunk->GetValue(0, 0).GetValue<int64_t>();
}
} // namespace

Database::Database(const config::Config &conf) {
  const auto db_pth = conf.db_conf.value_.db_pth;

  plog::init(plog::verbose, (db_pth.toString() + "/database.log").c_str());

  bool it_first_init = false;
  if (auto dir = Poco::File(db_pth); !dir.exists()) {
    dir.createDirectories();
    it_first_init = true;
  }

  mDb = std::make_unique<duckdb::DuckDB>(db_pth.toString() + "/database.db");

  // Reject future workspaces before any additive migration can touch their schema.
  {
    duckdb::Connection conn(*mDb);
    auto result = conn.Query("SELECT schema_version FROM ApplicationMetadata WHERE id = 1");
    if (result && !result->HasError()) {
      auto chunk = result->Fetch();
      if (chunk && chunk->size() && !chunk->GetValue(0, 0).IsNull()) {
        const auto version = chunk->GetValue(0, 0).GetValue<int32_t>();
        if (version != 1 && version != 2)
          throw std::runtime_error("Unsupported database schema version");
      }
    }
  }

  init_tables();
  apply_schema_migrations();
  init_application_metadata();
  init_payment_status_table();
  init_event_status_table();

  if (it_first_init) {
    add_demo_data();
  }
}

// --- Event ---

int64_t Database::add_event(const DuckEvent &event, const bool allowOverlap) {
  PLOG_DEBUG << "add_event request: start_ms="
             << event.start_date.value_or(-1)
             << ", end_ms=" << event.end_date.value_or(-1)
             << ", event_stat_id=" << event.event_stat_id
             << ", payment_stat_id=" << event.payment_stat_id;

  if (!allowOverlap && has_conflict(event)) {
    PLOG_WARNING << "Rejected event insert because of time conflict";
    return 0;
  }

  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  duckdb::vector<duckdb::Value> values{
      db_utils::toDuckValue(event.name),
      db_utils::toDuckValue(event.description),
      duckdb::Value::BOOLEAN(event.is_work_event),
      duckdb::Value::INTEGER(statusOrDefault(event.event_stat_id)),
      duckdb::Value::INTEGER(statusOrDefault(event.payment_stat_id)),
      db_utils::toDuckTimestamp(event.start_date.value_or(0) * 1000),
      db_utils::toDuckTimestamp(event.end_date.value_or(0) * 1000),
      db_utils::toDuckValue(event.duration),
      db_utils::toDuckValue(event.cost),
      duckdb::Value::BOOLEAN(event.is_online),
      duckdb::Value(event.meeting_url),
      db_utils::toDuckValue(event.series_id),
      timestampMsOrNull(event.original_occurrence_start),
      db_utils::toDuckValue(event.cancellation_reason),
      db_utils::toDuckValue(event.canceled_by),
      duckdb::Value::INTEGER(static_cast<int32_t>(event.buffer_before_minutes)),
      duckdb::Value::INTEGER(static_cast<int32_t>(event.buffer_after_minutes))};
  appendProviderValues(values, event.provider_kind, event.meeting_ref,
                       event.invitation_state);
  auto result = executePrepared(conn, constance::kInsertEventQuery, values);

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to insert event: " << result->GetError();
    return 0;
  }

  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    PLOG_ERROR << "Empty result from RETURNING id in add_event";
    return 0;
  }

  const auto newId =
      static_cast<int64_t>(chunk->GetValue(0, 0).GetValue<int32_t>());
  PLOG_DEBUG << "Inserted event id=" << newId;
  return newId;
}

bool Database::update_event(const DuckEvent &event, const bool allowOverlap) {
  if (event.id <= 0) {
    PLOG_WARNING << "Attempt to update event with invalid id: " << event.id;
    return false;
  }

  if (!allowOverlap && has_conflict(event)) {
    PLOG_WARNING << "Rejected event update because of time conflict for id="
                 << event.id;
    return false;
  }

  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  std::unique_ptr<DuckEvent> existingEvent;
  auto existingResult = executePrepared(conn, constance::kSelectEventByIdQuery,
                                        {duckdb::Value::BIGINT(event.id)});
  if (existingResult && !existingResult->HasError()) {
    if (auto chunk = existingResult->Fetch(); chunk && chunk->size() > 0) {
      existingEvent = std::make_unique<DuckEvent>(*chunk, 0);
    }
  }

  std::vector<int64_t> linkedClientIds;
  auto linkedClientsResult = executePrepared(
      conn, constance::kSelectClientIdsByEventIdQuery,
      {duckdb::Value::BIGINT(event.id)});
  if (!linkedClientsResult || linkedClientsResult->HasError()) {
    PLOG_ERROR << "Failed to fetch EventClient links for event (id=" << event.id
               << "): " << linkedClientsResult->GetError();
    return false;
  }

  while (auto chunk = linkedClientsResult->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      linkedClientIds.push_back(
          static_cast<int64_t>(chunk->GetValue(0, i).GetValue<int32_t>()));
    }
  }

  auto unlinkResult = executePrepared(
      conn, constance::kDeleteEventClientByEventIdQuery,
      {duckdb::Value::BIGINT(event.id)});
  if (!unlinkResult || unlinkResult->HasError()) {
    PLOG_ERROR << "Failed to unlink clients before event update (id="
               << event.id << "): " << unlinkResult->GetError();
    return false;
  }

  duckdb::vector<duckdb::Value> values{
      db_utils::toDuckValue(event.name),
      db_utils::toDuckValue(event.description),
      duckdb::Value::BOOLEAN(event.is_work_event),
      fkOrNull(event.event_stat_id),
      fkOrNull(event.payment_stat_id),
      db_utils::toDuckTimestamp(event.start_date.value_or(0) * 1000),
      db_utils::toDuckTimestamp(event.end_date.value_or(0) * 1000),
      db_utils::toDuckValue(event.duration),
      db_utils::toDuckValue(event.cost),
      duckdb::Value::BOOLEAN(event.is_online),
      duckdb::Value(event.meeting_url),
      db_utils::toDuckValue(event.series_id),
      timestampMsOrNull(event.original_occurrence_start),
      db_utils::toDuckValue(event.cancellation_reason),
      db_utils::toDuckValue(event.canceled_by),
      duckdb::Value::INTEGER(static_cast<int32_t>(event.buffer_before_minutes)),
      duckdb::Value::INTEGER(static_cast<int32_t>(event.buffer_after_minutes))};
  appendProviderValues(values, event.provider_kind, event.meeting_ref,
                       event.invitation_state);
  values.push_back(duckdb::Value::BIGINT(event.id));
  auto result = executePrepared(conn, constance::kUpdateEventQuery, values);

  if (!result || result->HasError()) {
    for (const auto clientId : linkedClientIds) {
      auto restoreResult = executePrepared(
          conn, constance::kInsertEventClientQuery,
          {duckdb::Value::BIGINT(clientId), duckdb::Value::BIGINT(event.id)});
      if (!restoreResult || restoreResult->HasError()) {
        PLOG_ERROR << "Failed to restore EventClient link for event (id="
                   << event.id << ", client_id=" << clientId
                   << "): " << restoreResult->GetError();
      }
    }
    PLOG_ERROR << "Failed to update event (id=" << event.id
               << "): " << result->GetError();
    return false;
  }

  if (existingEvent) {
    const auto nowMs = Poco::Timestamp().epochMicroseconds() / 1000;
    const auto occurredAt = db_utils::toDuckTimestamp(nowMs * 1000);
    const auto effectiveNewEventStat =
        event.event_stat_id > 0 ? event.event_stat_id : existingEvent->event_stat_id;
    const auto effectiveNewPaymentStat =
        event.payment_stat_id > 0 ? event.payment_stat_id : existingEvent->payment_stat_id;

    const auto insertChangeLogRow =
        [&](const int32_t changeKind, const char *label, const duckdb::Value &oldEventStat,
            const duckdb::Value &newEventStat, const duckdb::Value &oldPaymentStat,
            const duckdb::Value &newPaymentStat, const duckdb::Value &oldStart,
            const duckdb::Value &newStart, const duckdb::Value &reason) {
          auto logResult = executePrepared(
              conn, constance::kInsertEventChangeLogQuery,
              {duckdb::Value::BIGINT(event.id), duckdb::Value::INTEGER(changeKind), oldEventStat,
               newEventStat, oldPaymentStat, newPaymentStat, oldStart, newStart, reason,
               occurredAt});
          if (!logResult || logResult->HasError()) {
            PLOG_ERROR << "Failed to write EventChangeLog " << label
                       << " row for event (id=" << event.id
                       << "): " << (logResult ? logResult->GetError() : "prepare failed");
          }
        };

    if (effectiveNewEventStat != existingEvent->event_stat_id) {
      const auto reasonValue = (effectiveNewEventStat == 3 || effectiveNewEventStat == 5)
                                    ? db_utils::toDuckValue(event.cancellation_reason)
                                    : duckdb::Value();
      insertChangeLogRow(
          1, "status",
          duckdb::Value::INTEGER(static_cast<int32_t>(existingEvent->event_stat_id)),
          duckdb::Value::INTEGER(static_cast<int32_t>(effectiveNewEventStat)), duckdb::Value(),
          duckdb::Value(), duckdb::Value(), duckdb::Value(), reasonValue);
    }

    if (effectiveNewPaymentStat != existingEvent->payment_stat_id) {
      insertChangeLogRow(
          2, "payment", duckdb::Value(), duckdb::Value(),
          duckdb::Value::INTEGER(static_cast<int32_t>(existingEvent->payment_stat_id)),
          duckdb::Value::INTEGER(static_cast<int32_t>(effectiveNewPaymentStat)), duckdb::Value(),
          duckdb::Value(), duckdb::Value());
    }

    if (event.start_date.value_or(0) != existingEvent->start_date.value_or(0)) {
      insertChangeLogRow(3, "reschedule", duckdb::Value(), duckdb::Value(), duckdb::Value(),
                          duckdb::Value(), timestampMsOrNull(existingEvent->start_date),
                          timestampMsOrNull(event.start_date), duckdb::Value());
    }
  }

  return true;
}

bool Database::remove_event(const int64_t &id) {
  if (id <= 0) {
    PLOG_WARNING << "Attempt to remove event with invalid id: " << id;
    return false;
  }

  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  auto relationResult = executePrepared(
      conn, constance::kDeleteEventClientByEventIdQuery,
      {duckdb::Value::BIGINT(id)});
  if (!relationResult || relationResult->HasError()) {
    PLOG_ERROR << "Failed to delete EventClient links for event (id=" << id
               << "): " << relationResult->GetError();
    return false;
  }

  auto changeLogResult = executePrepared(
      conn, constance::kDeleteEventChangeLogByEventIdQuery, {duckdb::Value::BIGINT(id)});
  if (!changeLogResult || changeLogResult->HasError()) {
    PLOG_ERROR << "Failed to delete EventChangeLog rows for event (id=" << id
               << "): " << changeLogResult->GetError();
    return false;
  }

  for (const auto *query : {constance::kDetachTranscriptsOfEventQuery}) {
    auto transcriptResult = executePrepared(conn, query, {duckdb::Value::BIGINT(id)});
    if (!transcriptResult || transcriptResult->HasError()) {
      PLOG_ERROR << "Failed to detach transcripts of event (id=" << id << "): "
                 << (transcriptResult ? transcriptResult->GetError() : "prepare failed");
      return false;
    }
  }

  auto result =
      executePrepared(conn, constance::kDeleteEventByIdQuery, {duckdb::Value::BIGINT(id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to delete event (id=" << id
               << "): " << result->GetError();
    return false;
  }
  PLOG_DEBUG << "Event deleted: id=" << id;
  return true;
}

std::unique_ptr<DuckEvent> Database::get_event(const int64_t &id) {
  if (id <= 0) {
    PLOG_WARNING << "Attempt to get event with invalid id: " << id;
    return nullptr;
  }

  duckdb::Connection conn(*mDb);
  auto result =
      executePrepared(conn, constance::kSelectEventByIdQuery, {duckdb::Value::BIGINT(id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to query event (id=" << id
               << "): " << result->GetError();
    return nullptr;
  }

  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    PLOG_DEBUG << "Event not found: id=" << id;
    return nullptr;
  }

  return std::make_unique<DuckEvent>(*chunk, 0);
}

int64_t Database::add_event_series(const DuckEventSeries &series) {
  if (!series.start_date.has_value() || !series.end_date.has_value() ||
      series.recurrence_rule.empty()) {
    PLOG_WARNING << "Attempt to add invalid event series";
    return 0;
  }

  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  const auto nowMs = Poco::Timestamp().epochMicroseconds() / 1000;
  duckdb::vector<duckdb::Value> values{
      db_utils::toDuckValue(series.name),
      db_utils::toDuckValue(series.description),
      series.client_id.has_value() ? fkOrNull(*series.client_id) : duckdb::Value(),
      duckdb::Value::BOOLEAN(series.is_work_event),
      duckdb::Value::INTEGER(statusOrDefault(series.event_stat_id)),
      duckdb::Value::INTEGER(statusOrDefault(series.payment_stat_id)),
      timestampMsOrNull(series.start_date),
      timestampMsOrNull(series.end_date),
      db_utils::toDuckValue(series.duration),
      db_utils::toDuckValue(series.cost),
      duckdb::Value::BOOLEAN(series.is_online),
      duckdb::Value(series.meeting_url),
      duckdb::Value(series.recurrence_rule),
      timestampMsOrNull(series.recurrence_until),
      db_utils::toDuckTimestamp(std::make_optional(nowMs * 1000)),
      db_utils::toDuckValue(series.cancellation_reason),
      db_utils::toDuckValue(series.canceled_by),
      duckdb::Value::INTEGER(static_cast<int32_t>(series.buffer_before_minutes)),
      duckdb::Value::INTEGER(static_cast<int32_t>(series.buffer_after_minutes))};
  appendProviderValues(values, series.provider_kind, series.meeting_ref,
                       series.invitation_state);
  auto result = executePrepared(conn, constance::kInsertEventSeriesQuery, values);

  if (!result) {
    PLOG_ERROR << "Failed to prepare insert event series query";
    return 0;
  }

  if (result->HasError()) {
    PLOG_ERROR << "Failed to insert event series: " << result->GetError();
    return 0;
  }

  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    PLOG_ERROR << "Empty result from RETURNING id in add_event_series";
    return 0;
  }

  return static_cast<int64_t>(chunk->GetValue(0, 0).GetValue<int32_t>());
}

bool Database::update_event_series(const DuckEventSeries &series) {
  if (series.id <= 0 || !series.start_date.has_value() ||
      !series.end_date.has_value() || series.recurrence_rule.empty()) {
    PLOG_WARNING << "Attempt to update invalid event series";
    return false;
  }

  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  const auto nowMs = Poco::Timestamp().epochMicroseconds() / 1000;
  duckdb::vector<duckdb::Value> values{
      db_utils::toDuckValue(series.name),
      db_utils::toDuckValue(series.description),
      series.client_id.has_value() ? fkOrNull(*series.client_id) : duckdb::Value(),
      duckdb::Value::BOOLEAN(series.is_work_event),
      duckdb::Value::INTEGER(statusOrDefault(series.event_stat_id)),
      duckdb::Value::INTEGER(statusOrDefault(series.payment_stat_id)),
      timestampMsOrNull(series.start_date),
      timestampMsOrNull(series.end_date),
      db_utils::toDuckValue(series.duration),
      db_utils::toDuckValue(series.cost),
      duckdb::Value::BOOLEAN(series.is_online),
      duckdb::Value(series.meeting_url),
      duckdb::Value(series.recurrence_rule),
      timestampMsOrNull(series.recurrence_until),
      db_utils::toDuckTimestamp(std::make_optional(nowMs * 1000)),
      db_utils::toDuckValue(series.cancellation_reason),
      db_utils::toDuckValue(series.canceled_by),
      duckdb::Value::INTEGER(static_cast<int32_t>(series.buffer_before_minutes)),
      duckdb::Value::INTEGER(static_cast<int32_t>(series.buffer_after_minutes))};
  appendProviderValues(values, series.provider_kind, series.meeting_ref,
                       series.invitation_state);
  values.push_back(duckdb::Value::BIGINT(series.id));
  auto result = executePrepared(conn, constance::kUpdateEventSeriesQuery, values);

  if (!result) {
    PLOG_ERROR << "Failed to prepare update event series query";
    return false;
  }

  if (result->HasError()) {
    PLOG_ERROR << "Failed to update event series: " << result->GetError();
    return false;
  }
  return true;
}

bool Database::deactivate_event_series(const int64_t series_id) {
  if (series_id <= 0) {
    return false;
  }

  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  const auto nowMs = Poco::Timestamp().epochMicroseconds() / 1000;
  auto result = executePrepared(
      conn, constance::kDeactivateEventSeriesQuery,
      {duckdb::Value::BIGINT(series_id),
       db_utils::toDuckTimestamp(std::make_optional(nowMs * 1000))});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to deactivate event series: "
               << (result ? result->GetError() : "prepare failed");
    return false;
  }
  return true;
}

bool Database::delete_event_series_overrides_from(
    const int64_t series_id, const int64_t occurrence_start_ms) {
  if (series_id <= 0 || occurrence_start_ms <= 0) {
    return false;
  }

  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  const auto from = db_utils::toDuckTimestamp(std::make_optional(occurrence_start_ms * 1000));
  for (const auto *query : {constance::kDetachTranscriptsOfSeriesOverridesQuery}) {
    auto transcriptResult =
        executePrepared(conn, query, {duckdb::Value::BIGINT(series_id), from});
    if (!transcriptResult || transcriptResult->HasError()) {
      PLOG_ERROR << "Failed to detach transcripts of series overrides: "
                 << (transcriptResult ? transcriptResult->GetError() : "prepare failed");
      return false;
    }
  }

  auto result = executePrepared(
      conn, constance::kDeleteEventSeriesOverridesFromQuery,
      {duckdb::Value::BIGINT(series_id),
       db_utils::toDuckTimestamp(std::make_optional(occurrence_start_ms * 1000))});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to delete future event series overrides: "
               << (result ? result->GetError() : "prepare failed");
    return false;
  }
  return true;
}

std::unique_ptr<DuckEventSeries> Database::get_event_series(const int64_t series_id) {
  if (series_id <= 0) {
    return nullptr;
  }

  duckdb::Connection conn(*mDb);
  auto result = executePrepared(conn, constance::kSelectEventSeriesByIdQuery,
                                {duckdb::Value::BIGINT(series_id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to get event series: "
               << (result ? result->GetError() : "prepare failed");
    return nullptr;
  }

  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    return nullptr;
  }
  return std::make_unique<DuckEventSeries>(*chunk, 0);
}

std::vector<DuckEventSeries>
Database::get_event_series_for_range(const int64_t &start_ms,
                                     const int64_t &end_ms) {
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kSelectEventSeriesForRangeQuery,
      {db_utils::toDuckTimestamp(std::make_optional(end_ms * 1000)),
       db_utils::toDuckTimestamp(std::make_optional(start_ms * 1000))});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to get event series: " << result->GetError();
    return {};
  }

  std::vector<DuckEventSeries> series;
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      series.emplace_back(*chunk, i);
    }
  }
  return series;
}

std::vector<DuckEventSeries> Database::get_event_series_for_client_and_range(
    const int64_t client_id, const int64_t range_start_ms, const int64_t range_end_ms) {
  if (client_id <= 0) {
    return {};
  }

  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kSelectEventSeriesForClientAndRangeQuery,
      {duckdb::Value::BIGINT(client_id),
       db_utils::toDuckTimestamp(std::make_optional(range_end_ms * 1000)),
       db_utils::toDuckTimestamp(std::make_optional(range_start_ms * 1000))});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to fetch event series for client_id=" << client_id << ": "
               << (result ? result->GetError() : "prepare failed");
    return {};
  }

  std::vector<DuckEventSeries> series;
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      series.emplace_back(*chunk, i);
    }
  }

  return series;
}

std::set<std::pair<int64_t, int64_t>>
Database::get_event_series_exceptions_for_range(const int64_t &start_ms,
                                                const int64_t &end_ms) {
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kSelectEventSeriesExceptionsForRangeQuery,
      {db_utils::toDuckTimestamp(std::make_optional(start_ms * 1000)),
       db_utils::toDuckTimestamp(std::make_optional(end_ms * 1000))});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to get event series exceptions: " << result->GetError();
    return {};
  }

  std::set<std::pair<int64_t, int64_t>> exceptions;
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      const auto seriesId =
          static_cast<int64_t>(chunk->GetValue(0, i).GetValue<int32_t>());
      const auto occurrenceStart =
          db_utils::toOptionalTimestampMs(chunk->GetValue(1, i)).value_or(0);
      exceptions.insert({seriesId, occurrenceStart});
    }
  }
  return exceptions;
}

bool Database::add_event_series_exception(const int64_t series_id,
                                          const int64_t occurrence_start_ms,
                                          const std::string &reason) {
  if (series_id <= 0 || occurrence_start_ms <= 0) {
    return false;
  }

  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  auto result = executePrepared(
      conn, constance::kInsertEventSeriesExceptionQuery,
      {duckdb::Value::BIGINT(series_id),
       db_utils::toDuckTimestamp(std::make_optional(occurrence_start_ms * 1000)),
       reason.empty() ? duckdb::Value() : duckdb::Value(reason)});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to add event series exception: " << result->GetError();
    return false;
  }
  return true;
}

// --- Client ---

int64_t Database::add_client(const DuckClient &client) {
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kInsertClientQuery,
      {db_utils::toDuckValue(client.name),
       db_utils::toDuckValue(client.last_name),
       db_utils::toDuckValue(client.additional_info),
       db_utils::toDuckValue(client.diagnosis),
       timestampMsOrNull(client.birthday_date),
       db_utils::toDuckValue(client.email),
       db_utils::toDuckValue(client.phone_number),
       duckdb::Value::BOOLEAN(client.client_active),
       db_utils::toDuckValue(client.country),
       db_utils::toDuckValue(client.city),
       db_utils::toDuckValue(client.time_zone)});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to insert client: " << result->GetError();
    return 0;
  }

  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    PLOG_ERROR << "Empty result from RETURNING id in add_client";
    return 0;
  }

  return static_cast<int64_t>(chunk->GetValue(0, 0).GetValue<int32_t>());
}

bool Database::update_client(const DuckClient &client) {
  if (client.id <= 0) {
    PLOG_WARNING << "Attempt to update client with invalid id: " << client.id;
    return false;
  }

  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kUpdateClientQuery,
      {db_utils::toDuckValue(client.name),
       db_utils::toDuckValue(client.last_name),
       db_utils::toDuckValue(client.additional_info),
       db_utils::toDuckValue(client.diagnosis),
       timestampMsOrNull(client.birthday_date),
       db_utils::toDuckValue(client.email),
       db_utils::toDuckValue(client.phone_number),
       duckdb::Value::BOOLEAN(client.client_active),
       db_utils::toDuckValue(client.country),
       db_utils::toDuckValue(client.city),
       db_utils::toDuckValue(client.time_zone),
       duckdb::Value::BIGINT(client.id)});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to update client (id=" << client.id
               << "): " << result->GetError();
    return false;
  }

  return true;
}

std::unique_ptr<DuckClient> Database::get_client(const int64_t &id) {
  if (id <= 0) {
    PLOG_WARNING << "Attempt to get client with invalid id: " << id;
    return nullptr;
  }

  duckdb::Connection conn(*mDb);
  auto result =
      executePrepared(conn, constance::kSelectClientByIdQuery, {duckdb::Value::BIGINT(id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to query client (id=" << id
               << "): " << result->GetError();
    return nullptr;
  }

  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    PLOG_DEBUG << "Client not found: id=" << id;
    return nullptr;
  }

  return std::make_unique<DuckClient>(*chunk, 0);
}

std::vector<std::unique_ptr<DuckClient>> Database::get_clients() {
  duckdb::Connection conn(*mDb);
  auto result = conn.Query(constance::kSelectAllClientsQuery);
  if (result->HasError()) {
    PLOG_ERROR << "Failed to fetch all clients: " << result->GetError();
    return {};
  }

  std::vector<std::unique_ptr<DuckClient>> clients;
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      clients.emplace_back(std::make_unique<DuckClient>(*chunk, i));
    }
  }
  PLOG_DEBUG << "Loaded " << clients.size() << " clients";
  return clients;
}

std::vector<int64_t> Database::get_client_ids() {
  duckdb::Connection conn(*mDb);
  auto result = conn.Query(constance::kSelectAllClientIdsQuery);
  if (result->HasError()) {
    PLOG_ERROR << "Failed to fetch client IDs: " << result->GetError();
    return {};
  }

  std::vector<int64_t> ids;
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      ids.push_back(
          static_cast<int64_t>(chunk->GetValue(0, i).GetValue<int32_t>()));
    }
  }
  return ids;
}

bool Database::remove_client(const int64_t &id) {
  if (id <= 0) {
    PLOG_WARNING << "Attempt to remove client with invalid id: " << id;
    return false;
  }

  duckdb::Connection conn(*mDb);
  Transaction tx(conn);
  if (!tx.active()) return false;
  auto linkCheckResult = executePrepared(
      conn, constance::kHasClientEventsQuery,
      {duckdb::Value::BIGINT(id)});
  if (!linkCheckResult || linkCheckResult->HasError()) {
    PLOG_ERROR << "Failed to check client-event links (id=" << id
               << "): " << linkCheckResult->GetError();
    return false;
  }

  const auto hasLinkedEvents = [&]() {
    auto chunk = linkCheckResult->Fetch();
    return chunk && chunk->size() > 0;
  }();

  // Both hiding an event-linked card and deleting a standalone card remove its
  // transcript associations. Roll back the detach if the card mutation fails.
  auto transcriptLinks = executePrepared(conn,
      "DELETE FROM TranscriptClient WHERE client_id = $1", {duckdb::Value::BIGINT(id)});
  if (!transcriptLinks || transcriptLinks->HasError()) return false;

  if (hasLinkedEvents) {
    auto deactivateResult = executePrepared(
        conn, constance::kDeactivateClientByIdQuery,
        {duckdb::Value::BIGINT(id)});
    if (!deactivateResult || deactivateResult->HasError()) {
      PLOG_ERROR << "Failed to deactivate client (id=" << id
                 << "): " << deactivateResult->GetError();
      return false;
    }

    PLOG_DEBUG << "Client hidden by deactivation: id=" << id;
    return tx.commit();
  }

  auto unlinkResult = executePrepared(
      conn, constance::kDeleteEventClientByClientIdQuery,
      {duckdb::Value::BIGINT(id)});
  if (!unlinkResult || unlinkResult->HasError()) {
    PLOG_ERROR << "Failed to unlink client from events (id=" << id
               << "): " << unlinkResult->GetError();
    return false;
  }

  auto deleteResult = executePrepared(
      conn, constance::kDeleteClientByIdQuery, {duckdb::Value::BIGINT(id)});
  if (!deleteResult || deleteResult->HasError()) {
    PLOG_ERROR << "Failed to delete client (id=" << id
               << "): " << deleteResult->GetError();
    return false;
  }
  PLOG_DEBUG << "Client deleted: id=" << id;
  return tx.commit();
}

// --- EventClient ---

int64_t Database::add_event_client(const int64_t &event_id,
                                  const int64_t &client_id) {
  if (event_id <= 0) {
    PLOG_WARNING << "Invalid event_id for EventClient: " << event_id;
    return 0;
  }

  duckdb::Connection conn(*mDb);
  auto removeResult = executePrepared(
      conn, constance::kDeleteEventClientByEventIdQuery,
      {duckdb::Value::BIGINT(event_id)});
  if (!removeResult || removeResult->HasError()) {
    PLOG_ERROR << "Failed to clear EventClient links for event_id=" << event_id
               << ": " << removeResult->GetError();
    return 0;
  }

  if (client_id <= 0) {
    return 0;
  }

  auto result = executePrepared(
      conn, constance::kInsertEventClientQuery,
      {duckdb::Value::BIGINT(client_id), duckdb::Value::BIGINT(event_id)});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to link event_id=" << event_id
               << " and client_id=" << client_id << ": " << result->GetError();
    return 0;
  }

  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    PLOG_ERROR << "Empty result from RETURNING id in add_event_client";
    return 0;
  }

  return static_cast<int64_t>(chunk->GetValue(0, 0).GetValue<int32_t>());
}

int64_t Database::add_client_note(const DuckClientNote &note) {
  if (note.client_id <= 0) {
    PLOG_WARNING << "Invalid client_id for ClientNote: " << note.client_id;
    return 0;
  }

  const Poco::Timestamp now;
  const auto createdAtMs =
      note.created_at.value_or(static_cast<int64_t>(now.epochMicroseconds() / 1000));
  const auto updatedAtMs = note.updated_at.value_or(createdAtMs);

  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kInsertClientNoteQuery,
      duckdb::vector<duckdb::Value>{
          duckdb::Value::BIGINT(note.client_id),
          db_utils::toDuckValue(note.body_markdown),
          db_utils::toDuckTimestamp(createdAtMs * 1000),
          db_utils::toDuckTimestamp(updatedAtMs * 1000),
          db_utils::toDuckValue(note.linked_event_id),
          db_utils::toDuckValue(note.linked_series_id),
          note.linked_occurrence_start_ms.has_value()
              ? db_utils::toDuckTimestamp(std::make_optional(*note.linked_occurrence_start_ms * 1000))
              : db_utils::toDuckTimestamp(std::nullopt)});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to insert client note: " << result->GetError();
    return 0;
  }

  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    PLOG_ERROR << "Empty result from RETURNING id in add_client_note";
    return 0;
  }

  return static_cast<int64_t>(chunk->GetValue(0, 0).GetValue<int32_t>());
}

std::vector<DuckEvent> Database::get_events_for_client(const int64_t client_id) {
  if (client_id <= 0) {
    return {};
  }

  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kSelectEventsForClientQuery, {duckdb::Value::BIGINT(client_id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to fetch events for client_id=" << client_id << ": "
               << (result ? result->GetError() : "prepare failed");
    return {};
  }

  std::vector<DuckEvent> events;
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      events.emplace_back(*chunk, i);
    }
  }

  return events;
}

std::vector<DuckEventChangeLog>
Database::get_event_change_log_for_client(const int64_t client_id) {
  if (client_id <= 0) {
    return {};
  }

  duckdb::Connection conn(*mDb);
  auto result = executePrepared(conn, constance::kSelectEventChangeLogForClientQuery,
                                {duckdb::Value::BIGINT(client_id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to fetch event change log for client_id=" << client_id << ": "
               << (result ? result->GetError() : "prepare failed");
    return {};
  }

  std::vector<DuckEventChangeLog> entries;
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      entries.emplace_back(*chunk, i);
    }
  }

  return entries;
}

std::vector<DuckClientNote> Database::get_client_notes(const int64_t client_id) {
  if (client_id <= 0) {
    return {};
  }

  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kSelectClientNotesQuery, {duckdb::Value::BIGINT(client_id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to fetch client notes for client_id=" << client_id
               << ": " << result->GetError();
    return {};
  }

  std::vector<DuckClientNote> notes;
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      notes.emplace_back(*chunk, i);
    }
  }

  return notes;
}

int64_t Database::add_client_note_attachment(
    const DuckClientNoteAttachment &attachment) {
  if (attachment.note_id <= 0) {
    PLOG_WARNING << "Invalid note_id for ClientNoteAttachment: "
                 << attachment.note_id;
    return 0;
  }

  const Poco::Timestamp now;
  const auto createdAtMs =
      attachment.created_at.value_or(static_cast<int64_t>(now.epochMicroseconds() / 1000));

  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kInsertClientNoteAttachmentQuery,
      duckdb::vector<duckdb::Value>{
          duckdb::Value::BIGINT(attachment.note_id),
          db_utils::toDuckValue(attachment.file_name),
          db_utils::toDuckValue(attachment.relative_path),
          db_utils::toDuckValue(attachment.mime_type),
          db_utils::toDuckValue(attachment.size_bytes),
          db_utils::toDuckTimestamp(createdAtMs * 1000)});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to insert client note attachment: "
               << result->GetError();
    return 0;
  }

  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    PLOG_ERROR << "Empty result from RETURNING id in add_client_note_attachment";
    return 0;
  }

  return static_cast<int64_t>(chunk->GetValue(0, 0).GetValue<int32_t>());
}

std::vector<DuckClientNoteAttachment>
Database::get_note_attachments(const int64_t note_id) {
  if (note_id <= 0) {
    return {};
  }

  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kSelectNoteAttachmentsQuery,
      duckdb::vector<duckdb::Value>{duckdb::Value::BIGINT(note_id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to fetch note attachments for note_id=" << note_id
               << ": " << result->GetError();
    return {};
  }

  std::vector<DuckClientNoteAttachment> attachments;
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      attachments.emplace_back(*chunk, i);
    }
  }

  return attachments;
}

// --- Events by date / conflict ---

// std::vector<int64_t> Database::get_event_ids(const int64_t date_microseconds) {
//   // date_microseconds is already in microseconds (as in DuckEvent)
//   duckdb::Connection conn(*mDb);
//
//   // Assume we need events that start on this day.
//   // The exact rule should be clarified. For now: exact match by start_date.
//   auto result = conn.Query(
//       "SELECT id FROM Event",
//       db_utils::toDuckTimestamp(std::make_optional(date_microseconds)));
//
//   if (result->HasError()) {
//     PLOG_ERROR << "Failed to get event IDs: " << result->GetError();
//     return {};
//   }
//
//   std::vector<int64_t> ids;
//   while (auto chunk = result->Fetch()) {
//     for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
//       ids.push_back(
//           static_cast<int64_t>(chunk->GetValue(0, i).GetValue<int32_t>()));
//     }
//   }
//   return ids;
// }

bool Database::has_conflict(const DuckEvent &event) {
  return find_conflict(event).has_value();
}

std::optional<DuckEvent> Database::find_conflict(const DuckEvent &event) {
  if (!event.start_date.has_value() || !event.end_date.has_value()) {
    return std::nullopt;
  }

  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kHasConflictQuery,
      {duckdb::Value::BIGINT(event.id)});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Conflict check failed: " << result->GetError();
    return std::nullopt;
  }

  const auto candidateStart =
      *event.start_date - event.buffer_before_minutes * 60'000;
  const auto candidateEnd =
      *event.end_date + event.buffer_after_minutes * 60'000;

  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t index = 0; index < chunk->size(); ++index) {
      const auto existingStart =
          db_utils::toOptionalTimestampMs(chunk->GetValue(1, index));
      const auto existingEnd =
          db_utils::toOptionalTimestampMs(chunk->GetValue(2, index));
      if (!existingStart.has_value() || !existingEnd.has_value()) {
        continue;
      }

      const auto bufferBefore =
          db_utils::toOptionalInt32AsInt64(chunk->GetValue(3, index)).value_or(0);
      const auto bufferAfter =
          db_utils::toOptionalInt32AsInt64(chunk->GetValue(4, index)).value_or(0);
      const auto existingEffectiveStart = *existingStart - bufferBefore * 60'000;
      const auto existingEffectiveEnd = *existingEnd + bufferAfter * 60'000;
      if (candidateStart < existingEffectiveEnd &&
          candidateEnd > existingEffectiveStart) {
        const auto conflictingId =
            db_utils::toOptionalInt32AsInt64(chunk->GetValue(0, index));
        if (!conflictingId.has_value()) {
          continue;
        }
        if (auto conflictingEvent = get_event(*conflictingId)) {
          return *conflictingEvent;
        }
        return std::nullopt;
      }
    }
  }

  return std::nullopt;
}

std::vector<DuckEvent>
Database::get_day_events(const int64_t &start_ms, const int64_t &end_ms) {
  const auto start_day = start_ms * 1000;
  const auto end_day = end_ms * 1000;
  PLOG_DEBUG << "get_day_events for range_ms=[" << start_ms << ", " << end_ms
             << "] range_micros=[" << start_day << ", " << end_day << "]";

  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kSelectDayEventsQuery,
      {db_utils::toDuckTimestamp(std::make_optional(end_day)),
       db_utils::toDuckTimestamp(std::make_optional(start_day))});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to get day events: " << result->GetError();
    return {};
  }

  std::vector<DuckEvent> events;
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      events.emplace_back(*chunk, i);
      const auto &ev = events.back();
      PLOG_DEBUG << "get_day_events row id=" << ev.id
                 << " start_ms=" << ev.start_date.value_or(-1)
                 << " end_ms=" << ev.end_date.value_or(-1);
    }
  }
  PLOG_DEBUG << "get_day_events loaded count=" << events.size();
  return events;
}

std::vector<DuckEvent>
Database::get_upcoming_events(const int64_t &start_ms, const int64_t &end_ms) {
  if (end_ms < start_ms) {
    return {};
  }

  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kSelectUpcomingEventsQuery,
      {db_utils::toDuckTimestamp(std::make_optional(start_ms * 1000)),
       db_utils::toDuckTimestamp(std::make_optional(end_ms * 1000))});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to get upcoming events: " << result->GetError();
    return {};
  }

  std::vector<DuckEvent> events;
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      events.emplace_back(*chunk, i);
    }
  }

  return events;
}

bool Database::mark_event_reminder_notified(const int64_t &event_id,
                                            const int64_t &notified_at_ms) {
  if (event_id <= 0 || notified_at_ms <= 0) {
    return false;
  }

  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kMarkEventReminderNotifiedQuery,
      {duckdb::Value::BIGINT(event_id),
       db_utils::toDuckTimestamp(std::make_optional(notified_at_ms * 1000))});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to mark reminder as notified for event " << event_id
               << ": " << result->GetError();
    return false;
  }

  return true;
}

std::set<std::pair<int64_t, int64_t>>
Database::get_notified_series_occurrences_for_range(const int64_t &start_ms,
                                                     const int64_t &end_ms) {
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kSelectNotifiedSeriesOccurrencesForRangeQuery,
      {db_utils::toDuckTimestamp(std::make_optional(start_ms * 1000)),
       db_utils::toDuckTimestamp(std::make_optional(end_ms * 1000))});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to get notified series occurrences: " << result->GetError();
    return {};
  }

  std::set<std::pair<int64_t, int64_t>> notified;
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      const auto seriesId =
          static_cast<int64_t>(chunk->GetValue(0, i).GetValue<int32_t>());
      const auto occurrenceStart =
          db_utils::toOptionalTimestampMs(chunk->GetValue(1, i)).value_or(0);
      notified.insert({seriesId, occurrenceStart});
    }
  }
  return notified;
}

bool Database::mark_series_occurrence_reminder_notified(
    const int64_t &series_id, const int64_t &occurrence_start_ms,
    const int64_t &notified_at_ms) {
  if (series_id <= 0 || occurrence_start_ms <= 0 || notified_at_ms <= 0) {
    return false;
  }

  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kMarkSeriesOccurrenceReminderNotifiedQuery,
      {duckdb::Value::BIGINT(series_id),
       db_utils::toDuckTimestamp(std::make_optional(occurrence_start_ms * 1000)),
       db_utils::toDuckTimestamp(std::make_optional(notified_at_ms * 1000))});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to mark series occurrence reminder as notified for series "
               << series_id << ": " << result->GetError();
    return false;
  }

  return true;
}

std::set<int64_t> Database::get_materialized_occurrence_starts_for_series(
    const int64_t &series_id) {
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kSelectMaterializedOccurrenceStartsForSeriesQuery,
      {duckdb::Value::BIGINT(series_id)});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to get materialized occurrence starts for series "
               << series_id << ": " << result->GetError();
    return {};
  }

  std::set<int64_t> starts;
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      const auto occurrenceStart =
          db_utils::toOptionalTimestampMs(chunk->GetValue(0, i)).value_or(0);
      starts.insert(occurrenceStart);
    }
  }
  return starts;
}

std::unique_ptr<DuckEvent> Database::get_event_by_series_occurrence(
    const int64_t series_id, const int64_t occurrence_start_ms) {
  if (series_id <= 0) {
    return nullptr;
  }

  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kSelectEventBySeriesOccurrenceQuery,
      {duckdb::Value::BIGINT(series_id),
       db_utils::toDuckTimestamp(std::make_optional(occurrence_start_ms * 1000))});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to query event by series occurrence (series_id=" << series_id
               << "): " << (result ? result->GetError() : "prepare failed");
    return nullptr;
  }

  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    return nullptr;
  }

  return std::make_unique<DuckEvent>(*chunk, 0);
}

std::vector<ClientMonthlyStats>
Database::get_client_monthly_stats(const int64_t &client_id, const int months_back) {
  if (client_id <= 0 || months_back <= 0) {
    return {};
  }

  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kSelectClientMonthlyStatsQuery,
      {duckdb::Value::BIGINT(client_id), duckdb::Value::INTEGER(months_back)});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to get monthly stats for client " << client_id << ": "
               << result->GetError();
    return {};
  }

  std::vector<ClientMonthlyStats> stats;
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      ClientMonthlyStats item;
      item.year = chunk->GetValue(0, i).GetValue<int32_t>();
      item.month = chunk->GetValue(1, i).GetValue<int32_t>();
      item.sessions = chunk->GetValue(2, i).GetValue<int64_t>();
      item.income = chunk->GetValue(3, i).GetValue<double>();
      stats.push_back(item);
    }
  }

  return stats;
}

DashboardSummary Database::get_dashboard_summary() {
  duckdb::Connection conn(*mDb);
  auto result = conn.Query(constance::kSelectDashboardSummaryQuery);

  DashboardSummary summary;
  if (result->HasError()) {
    PLOG_ERROR << "Failed to get dashboard summary: " << result->GetError();
    return summary;
  }

  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    return summary;
  }

  summary.total_clients = chunk->GetValue(0, 0).GetValue<int64_t>();
  summary.active_clients = chunk->GetValue(1, 0).GetValue<int64_t>();
  summary.sessions_this_month = chunk->GetValue(2, 0).GetValue<int64_t>();
  summary.work_sessions_this_month = chunk->GetValue(3, 0).GetValue<int64_t>();
  summary.personal_sessions_this_month = chunk->GetValue(4, 0).GetValue<int64_t>();
  summary.income_this_month = chunk->GetValue(5, 0).GetValue<double>();
  return summary;
}

std::vector<DashboardMonthlyStats>
Database::get_dashboard_monthly_stats(const int months_back) {
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kSelectDashboardMonthlyStatsQuery,
      {duckdb::Value::INTEGER(months_back)});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to get dashboard monthly stats: " << result->GetError();
    return {};
  }

  std::vector<DashboardMonthlyStats> stats;
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      DashboardMonthlyStats item;
      item.year = chunk->GetValue(0, i).GetValue<int32_t>();
      item.month = chunk->GetValue(1, i).GetValue<int32_t>();
      item.sessions = chunk->GetValue(2, i).GetValue<int64_t>();
      item.work_sessions = chunk->GetValue(3, i).GetValue<int64_t>();
      item.personal_sessions = chunk->GetValue(4, i).GetValue<int64_t>();
      item.income = chunk->GetValue(5, i).GetValue<double>();
      stats.push_back(item);
    }
  }

  return stats;
}

DuckClient Database::get_client_by_event(const int64_t &event_id) {
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kSelectClientByEventQuery, {duckdb::Value::BIGINT(event_id)});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to get client for event " << event_id << ": "
               << result->GetError();
    throw std::runtime_error("Client not found for event: " +
                             std::to_string(event_id));
  }

  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    throw std::runtime_error("Client not found for event: " +
                             std::to_string(event_id));
  }

  return DuckClient(*chunk, 0);
}

DuckApplicationMetadata Database::get_application_metadata() const {
  duckdb::Connection conn(*mDb);
  auto result = conn.Query(constance::kSelectApplicationMetadata);
  if (!result || result->HasError()) {
    throw std::runtime_error("Failed to read application metadata: " +
                             (result ? result->GetError() : "unknown error"));
  }

  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    throw std::runtime_error("Application metadata is not initialized");
  }
  return DuckApplicationMetadata(*chunk, 0);
}

bool Database::export_snapshot(const std::string &target_dir) const {
  duckdb::Connection conn(*mDb);
  std::string escaped_dir;
  escaped_dir.reserve(target_dir.size());
  for (const char ch : target_dir) {
    if (ch == '\'') {
      escaped_dir += "''";
    } else {
      escaped_dir += ch;
    }
  }

  const auto query =
      "EXPORT DATABASE '" + escaped_dir + "' (FORMAT PARQUET);";
  auto result = conn.Query(query);
  if (!result || result->HasError()) {
    PLOG_ERROR << "export_snapshot failed: "
               << (result ? result->GetError() : std::string{"null result"});
    return false;
  }
  return true;
}

// --- Recurring schedule identity and transactional outbox ---

duckdb::Connection &Database::write_connection(std::optional<duckdb::Connection> &owned) {
  if (mTxConn != nullptr) {
    assert(mTxThread == std::this_thread::get_id() &&
           "schedule transaction connection used from another thread");
    if (mTxThread == std::this_thread::get_id()) {
      return *mTxConn;
    }
    // Release builds: never share the transaction's connection across threads.
    PLOG_ERROR << "Schedule transaction connection requested from another thread";
  }
  owned.emplace(*mDb);
  return *owned;
}

std::optional<ScheduleIdentity>
Database::read_schedule_identity(duckdb::Connection &conn, const char *query,
                                 duckdb::Value key) {
  auto result = executePrepared(conn, query, {std::move(key)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to read schedule identity: "
               << (result ? result->GetError() : "prepare failed");
    return std::nullopt;
  }
  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    return std::nullopt;
  }
  return identityFromChunk(*chunk, 0);
}

bool Database::execute_schedule_update(duckdb::Connection &conn, const char *query,
                                       duckdb::vector<duckdb::Value> values) {
  auto result = executePrepared(conn, query, std::move(values));
  if (!result || result->HasError()) {
    PLOG_ERROR << "Schedule update failed: "
               << (result ? result->GetError() : "prepare failed");
    return false;
  }
  return true;
}

std::optional<ScheduleCommit>
Database::commit_schedule_change(const ScheduleMutation &mutation,
                                 const std::string &timezone,
                                 const SchedulePayloadBuilder &builder) {
  if (mTxConn != nullptr) {
    PLOG_ERROR << "Nested schedule transaction rejected";
    return std::nullopt;
  }

  duckdb::Connection conn(*mDb);
  Transaction tx(conn);
  if (!tx.active()) {
    PLOG_ERROR << "Failed to begin schedule transaction";
    return std::nullopt;
  }

  struct ActiveScope {
    Database &db;
    ActiveScope(Database &d, duckdb::Connection &c) : db(d) {
      db.mTxThread = std::this_thread::get_id();
      db.mTxConn = &c;
    }
    ~ActiveScope() { db.mTxConn = nullptr; }
  } scope(*this, conn);

  try {
    const auto changed = mutation();
    if (!changed.has_value() || *changed <= 0) {
      return std::nullopt;
    }
    const auto seriesId = *changed;

    auto identity = read_schedule_identity(
        conn, constance::kSelectScheduleIdentityBySeriesQuery,
        duckdb::Value::BIGINT(seriesId));
    if (!identity.has_value()) {
      if (timezone.empty()) {
        // Legacy series: local change only, nothing to publish.
        if (!tx.commit()) {
          return std::nullopt;
        }
        return ScheduleCommit{seriesId, {}, 0};
      }
      const auto uid = Poco::UUIDGenerator::defaultGenerator().createRandom().toString();
      if (!execute_schedule_update(conn, constance::kInsertScheduleIdentityQuery,
                                   {duckdb::Value::BIGINT(seriesId), duckdb::Value(uid),
                                    duckdb::Value(timezone), nowTimestamp()})) {
        return std::nullopt;
      }
    }
    if (!execute_schedule_update(conn, constance::kBumpScheduleDesiredRevisionQuery,
                                 {duckdb::Value::BIGINT(seriesId), nowTimestamp()})) {
      return std::nullopt;
    }
    identity = read_schedule_identity(conn, constance::kSelectScheduleIdentityBySeriesQuery,
                                      duckdb::Value::BIGINT(seriesId));
    if (!identity.has_value()) {
      return std::nullopt;
    }

    ScheduleSource source;
    source.identity = *identity;
    {
      auto seriesResult = executePrepared(conn, constance::kSelectEventSeriesByIdQuery,
                                          {duckdb::Value::BIGINT(seriesId)});
      if (!seriesResult || seriesResult->HasError()) {
        return std::nullopt;
      }
      auto chunk = seriesResult->Fetch();
      if (!chunk || chunk->size() == 0) {
        return std::nullopt;
      }
      source.series = DuckEventSeries(*chunk, 0);
    }
    {
      auto overrides = executePrepared(conn, constance::kSelectScheduleOverridesQuery,
                                       {duckdb::Value::BIGINT(seriesId)});
      if (!overrides || overrides->HasError()) {
        return std::nullopt;
      }
      while (auto chunk = overrides->Fetch()) {
        for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
          ScheduleOverrideSource item;
          item.original_start_ms =
              db_utils::toOptionalTimestampMs(chunk->GetValue(0, i)).value_or(0);
          item.start_ms = db_utils::toOptionalTimestampMs(chunk->GetValue(1, i)).value_or(0);
          item.end_ms = db_utils::toOptionalTimestampMs(chunk->GetValue(2, i)).value_or(0);
          item.event_stat_id = db_utils::toInt32AsInt64(chunk->GetValue(3, i));
          source.overrides.push_back(item);
        }
      }
    }
    {
      auto exceptions = executePrepared(conn, constance::kSelectScheduleExceptionsQuery,
                                        {duckdb::Value::BIGINT(seriesId)});
      if (!exceptions || exceptions->HasError()) {
        return std::nullopt;
      }
      while (auto chunk = exceptions->Fetch()) {
        for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
          source.exceptions.push_back(
              db_utils::toOptionalTimestampMs(chunk->GetValue(0, i)).value_or(0));
        }
      }
    }

    const auto payload = builder(source);
    if (!payload.has_value()) {
      PLOG_WARNING << "Schedule payload builder rejected the change; rolling back";
      return std::nullopt;
    }

    auto outbox = executePrepared(conn, constance::kSelectScheduleOutboxQuery,
                                  {duckdb::Value(identity->series_uid)});
    if (!outbox || outbox->HasError()) {
      return std::nullopt;
    }
    const auto outboxChunk = outbox->Fetch();
    const bool outboxExists = outboxChunk && outboxChunk->size() > 0;
    const duckdb::vector<duckdb::Value> values{
        duckdb::Value(identity->series_uid), duckdb::Value(*payload),
        duckdb::Value::BIGINT(identity->desired_revision), nowTimestamp()};
    if (!execute_schedule_update(conn,
                                 outboxExists ? constance::kUpdateScheduleOutboxPendingQuery
                                              : constance::kInsertScheduleOutboxQuery,
                                 values)) {
      return std::nullopt;
    }

    if (!tx.commit()) {
      return std::nullopt;
    }
    return ScheduleCommit{seriesId, identity->series_uid, identity->desired_revision};
  } catch (const std::exception &error) {
    PLOG_ERROR << "Schedule transaction aborted: " << error.what();
    return std::nullopt;
  } catch (...) {
    PLOG_ERROR << "Schedule transaction aborted";
    return std::nullopt;
  }
}

std::optional<ScheduleIdentity> Database::get_schedule_identity(const int64_t series_id) {
  duckdb::Connection conn(*mDb);
  return read_schedule_identity(conn, constance::kSelectScheduleIdentityBySeriesQuery,
                                duckdb::Value::BIGINT(series_id));
}

std::optional<ScheduleIdentity>
Database::get_schedule_identity_by_uid(const std::string &series_uid) {
  duckdb::Connection conn(*mDb);
  return read_schedule_identity(conn, constance::kSelectScheduleIdentityByUidQuery,
                                duckdb::Value(series_uid));
}

std::optional<ScheduleOutbox> Database::get_schedule_outbox(const std::string &series_uid) {
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(conn, constance::kSelectScheduleOutboxQuery,
                                {duckdb::Value(series_uid)});
  if (!result || result->HasError()) {
    return std::nullopt;
  }
  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    return std::nullopt;
  }
  ScheduleOutbox outbox;
  outbox.series_uid = chunk->GetValue(0, 0).ToString();
  outbox.pending_payload = db_utils::toOptionalString(chunk->GetValue(1, 0));
  outbox.pending_desired_revision = optionalBigint(chunk->GetValue(2, 0));
  outbox.inflight_revision = optionalBigint(chunk->GetValue(3, 0));
  outbox.inflight_payload = db_utils::toOptionalString(chunk->GetValue(4, 0));
  outbox.inflight_hash = db_utils::toOptionalString(chunk->GetValue(5, 0));
  outbox.inflight_desired_revision = optionalBigint(chunk->GetValue(6, 0));
  return outbox;
}

std::vector<ScheduleIdentity> Database::list_schedule_series_pending_sync() {
  duckdb::Connection conn(*mDb);
  std::vector<ScheduleIdentity> identities;
  auto result = conn.Query(constance::kSelectScheduleSeriesPendingSyncQuery);
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to list pending schedule series";
    return identities;
  }
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      if (auto identity = identityFromChunk(*chunk, i)) {
        identities.push_back(std::move(*identity));
      }
    }
  }
  return identities;
}

bool Database::freeze_schedule_pending(const std::string &series_uid,
                                       const int64_t expected_pending_desired,
                                       const int64_t revision,
                                       const std::string &payload,
                                       const std::string &content_hash) {
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kFreezeScheduleOutboxQuery,
      {duckdb::Value(series_uid), duckdb::Value::BIGINT(expected_pending_desired),
       duckdb::Value::BIGINT(revision), duckdb::Value(payload),
       duckdb::Value(content_hash), nowTimestamp()});
  const auto rows = affectedRows(result.get());
  return rows.has_value() && *rows == 1;
}

std::optional<ScheduleAck>
Database::ack_schedule_inflight(const std::string &series_uid, const int64_t revision,
                                const std::string &content_hash) {
  duckdb::Connection conn(*mDb);
  Transaction tx(conn);
  if (!tx.active()) {
    return std::nullopt;
  }

  auto outboxResult = executePrepared(conn, constance::kSelectScheduleOutboxQuery,
                                      {duckdb::Value(series_uid)});
  if (!outboxResult || outboxResult->HasError()) {
    return std::nullopt;
  }
  auto chunk = outboxResult->Fetch();
  if (!chunk || chunk->size() == 0) {
    return std::nullopt;
  }
  const auto inflightRevision = optionalBigint(chunk->GetValue(3, 0));
  if (!inflightRevision.has_value() || *inflightRevision != revision) {
    return std::nullopt;
  }
  const bool hasNewer = !chunk->GetValue(1, 0).IsNull();

  if (!execute_schedule_update(conn, constance::kClearScheduleInflightQuery,
                               {duckdb::Value(series_uid), nowTimestamp()}) ||
      !execute_schedule_update(
          conn, constance::kAckScheduleIdentityQuery,
          {duckdb::Value(series_uid), duckdb::Value::BIGINT(revision),
           duckdb::Value(content_hash),
           duckdb::Value(hasNewer ? schedule_sync_state::kPending
                                  : schedule_sync_state::kSynced),
           nowTimestamp()})) {
    return std::nullopt;
  }
  if (!tx.commit()) {
    return std::nullopt;
  }
  return ScheduleAck{hasNewer};
}

bool Database::release_schedule_inflight(const std::string &series_uid,
                                         const std::string &pending_payload) {
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kReleaseScheduleInflightQuery,
      {duckdb::Value(series_uid), duckdb::Value(pending_payload), nowTimestamp()});
  const auto rows = affectedRows(result.get());
  return rows.has_value() && *rows == 1;
}

std::optional<std::vector<std::string>>
Database::clear_series_legacy_meeting(const int64_t series_id) {
  if (series_id <= 0) {
    return std::nullopt;
  }
  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  std::optional<Transaction> tx;
  if (mTxConn == nullptr) {
    tx.emplace(conn);
    if (!tx->active()) {
      return std::nullopt;
    }
  }

  std::vector<std::string> refs;
  auto selected = executePrepared(conn, constance::kSelectSeriesLegacyMeetingRefsQuery,
                                  {duckdb::Value::BIGINT(series_id)});
  if (!selected || selected->HasError()) {
    return std::nullopt;
  }
  while (auto chunk = selected->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      refs.push_back(chunk->GetValue(0, i).ToString());
    }
  }
  if (!execute_schedule_update(conn, constance::kClearSeriesLegacyMeetingQuery,
                               {duckdb::Value::BIGINT(series_id), nowTimestamp()}) ||
      !execute_schedule_update(conn, constance::kClearSeriesEventsLegacyMeetingQuery,
                               {duckdb::Value::BIGINT(series_id)})) {
    return std::nullopt;
  }
  if (tx && !tx->commit()) {
    return std::nullopt;
  }
  return refs;
}

bool Database::set_schedule_sync_state(const std::string &series_uid,
                                       const std::string &state,
                                       const std::string &error) {
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kSetScheduleSyncStateQuery,
      {duckdb::Value(series_uid), duckdb::Value(state),
       error.empty() ? duckdb::Value() : duckdb::Value(error), nowTimestamp()});
  const auto rows = affectedRows(result.get());
  return rows.has_value() && *rows == 1;
}

bool Database::adopt_schedule_server_revision(const std::string &series_uid,
                                              const int64_t server_revision,
                                              const std::string &content_hash,
                                              const bool allow_rewind) {
  // Usable on its own or as part of a commit_schedule_change() mutation, in
  // which case it joins that transaction instead of opening a nested one.
  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  std::optional<Transaction> tx;
  if (mTxConn == nullptr) {
    tx.emplace(conn);
    if (!tx->active()) {
      return false;
    }
  }
  const auto current = read_schedule_identity(
      conn, constance::kSelectScheduleIdentityByUidQuery, duckdb::Value(series_uid));
  if (!current.has_value() ||
      (!allow_rewind && server_revision < current->acked_revision)) {
    return false;
  }
  auto adopted = executePrepared(
      conn, constance::kAdoptScheduleServerRevisionQuery,
      {duckdb::Value(series_uid), duckdb::Value::BIGINT(server_revision),
       duckdb::Value(content_hash), nowTimestamp()});
  const auto rows = affectedRows(adopted.get());
  if (!rows.has_value() || *rows != 1) {
    return false;
  }
  if (!execute_schedule_update(conn, constance::kClearScheduleOutboxQuery,
                               {duckdb::Value(series_uid), nowTimestamp()})) {
    return false;
  }
  return !tx.has_value() || tx->commit();
}

std::string Database::ensure_schedule_invitation_key(const int64_t series_id) {
  duckdb::Connection conn(*mDb);
  Transaction tx(conn);
  if (!tx.active()) {
    return {};
  }
  const auto identity = read_schedule_identity(
      conn, constance::kSelectScheduleIdentityBySeriesQuery, duckdb::Value::BIGINT(series_id));
  if (!identity.has_value()) {
    return {};
  }
  if (identity->invitation_key.has_value() && !identity->invitation_key->empty()) {
    return *identity->invitation_key;
  }
  const auto key = Poco::UUIDGenerator::defaultGenerator().createRandom().toString();
  if (!execute_schedule_update(conn, constance::kSetScheduleInvitationKeyQuery,
                               {duckdb::Value::BIGINT(series_id), duckdb::Value(key),
                                nowTimestamp()}) ||
      !tx.commit()) {
    return {};
  }
  return key;
}

bool Database::clear_schedule_invitation_key(const int64_t series_id) {
  duckdb::Connection conn(*mDb);
  return execute_schedule_update(conn, constance::kSetScheduleInvitationKeyQuery,
                                 {duckdb::Value::BIGINT(series_id), duckdb::Value(),
                                  nowTimestamp()});
}

bool Database::set_schedule_invitation_generation(const int64_t series_id,
                                                  const int64_t generation) {
  duckdb::Connection conn(*mDb);
  return execute_schedule_update(conn, constance::kSetScheduleInvitationGenerationQuery,
                                 {duckdb::Value::BIGINT(series_id),
                                  duckdb::Value::BIGINT(generation), nowTimestamp()});
}

// --- Live call transcripts ---

namespace {

bool eventExists(duckdb::Connection &conn, const int64_t id) {
  auto result = executePrepared(conn, constance::kEventExistsQuery,
                                {duckdb::Value::BIGINT(id)});
  if (!result || result->HasError()) return false;
  auto chunk = result->Fetch();
  return chunk && chunk->size() > 0;
}

}  // namespace

int64_t Database::add_transcript(const std::optional<int64_t> event_id, const std::string &consent_scope,
                                 const std::optional<std::string> &model_id,
                                 const std::optional<int64_t> consent_given_at_ms) {
  if (event_id && *event_id <= 0) {
    PLOG_WARNING << "Invalid event_id for Transcript";
    return 0;
  }
  const auto now = nowMs();
  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  std::optional<Transaction> tx;
  if (ownedConn) {
    tx.emplace(conn);
    if (!tx->active()) return 0;
  }
  if (event_id && !eventExists(conn, *event_id)) {
    PLOG_WARNING << "Rejected transcript for unknown event";
    return 0;
  }
  auto result = executePrepared(
      conn, constance::kInsertTranscriptQuery,
      {db_utils::toDuckValue(event_id), duckdb::Value(consent_scope),
       db_utils::toDuckTimestamp(consent_given_at_ms.value_or(now) * 1000),
       db_utils::toDuckValue(model_id), db_utils::toDuckTimestamp(now * 1000),
       db_utils::toDuckTimestamp(now * 1000)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to insert transcript: " << (result ? result->GetError() : "prepare failed");
    return 0;
  }
  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    PLOG_ERROR << "Empty result from RETURNING id in add_transcript";
    return 0;
  }
  const auto id = static_cast<int64_t>(chunk->GetValue(0, 0).GetValue<int32_t>());
  if (event_id) {
    auto links = executePrepared(conn,
        "INSERT INTO TranscriptClient SELECT DISTINCT $1, client_id FROM EventClient WHERE event_id = $2",
        {duckdb::Value::BIGINT(id), duckdb::Value::BIGINT(*event_id)});
    if (!links || links->HasError()) return 0;
  }
  if (tx && !tx->commit()) return 0;
  return id;
}

std::unique_ptr<DuckTranscript> Database::get_transcript(const int64_t id) {
  if (id <= 0) return nullptr;
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(conn, constance::kSelectTranscriptByIdQuery,
                                {duckdb::Value::BIGINT(id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to get transcript (id=" << id
               << "): " << (result ? result->GetError() : "prepare failed");
    return nullptr;
  }
  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) return nullptr;
  return std::make_unique<DuckTranscript>(*chunk, 0);
}

std::vector<DuckTranscript> Database::get_transcripts_for_event(const int64_t event_id) {
  std::vector<DuckTranscript> out;
  if (event_id <= 0) return out;
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(conn, constance::kSelectTranscriptsByEventQuery,
                                {duckdb::Value::BIGINT(event_id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to list transcripts (event_id=" << event_id
               << "): " << (result ? result->GetError() : "prepare failed");
    return out;
  }
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) out.emplace_back(*chunk, i);
  }
  return out;
}

namespace {

void checkpointBestEffort(duckdb::Connection &conn, const char *what) {
  auto result = conn.Query("CHECKPOINT");
  if (!result || result->HasError()) {
    PLOG_ERROR << "CHECKPOINT after " << what
               << " failed: " << (result ? result->GetError() : "no result");
  }
}

bool transcriptExists(duckdb::Connection &conn, const int64_t id) {
  auto result = executePrepared(conn, constance::kTranscriptExistsQuery,
                                {duckdb::Value::BIGINT(id)});
  if (!result || result->HasError()) return false;
  auto chunk = result->Fetch();
  return chunk && chunk->size() > 0;
}

}  // namespace

bool Database::set_transcript_status(const int64_t id, const std::string &status) {
  if (id <= 0 || (status != "recording" && status != "draft" && status != "reviewed")) {
    PLOG_WARNING << "Rejected transcript status change (id=" << id << ")";
    return false;
  }
  std::optional<duckdb::Connection> ownedConn;
  // Own connection on purpose: transcript writes run on the session's executor/cleanup
  // thread and must not join (or race) a schedule transaction.
  ownedConn.emplace(*mDb);
  auto &conn = *ownedConn;
  if (!transcriptExists(conn, id)) return false;
  auto result = executePrepared(
      conn, constance::kUpdateTranscriptStatusQuery,
      {duckdb::Value(status), db_utils::toDuckTimestamp(nowMs() * 1000),
       duckdb::Value::BIGINT(id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to update transcript status (id=" << id
               << "): " << (result ? result->GetError() : "prepare failed");
    return false;
  }
  return true;
}

bool Database::revoke_transcript_consent(const int64_t id,
                                         const std::optional<int64_t> at_ms) {
  if (id <= 0) return false;
  const auto now = nowMs();
  std::optional<duckdb::Connection> ownedConn;
  // Own connection on purpose: transcript writes run on the session's executor/cleanup
  // thread and must not join (or race) a schedule transaction.
  ownedConn.emplace(*mDb);
  auto &conn = *ownedConn;
  if (!transcriptExists(conn, id)) return false;
  auto result = executePrepared(
      conn, constance::kRevokeTranscriptConsentQuery,
      {db_utils::toDuckTimestamp(at_ms.value_or(now) * 1000),
       db_utils::toDuckTimestamp(now * 1000), duckdb::Value::BIGINT(id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to revoke transcript consent (id=" << id
               << "): " << (result ? result->GetError() : "prepare failed");
    return false;
  }
  return true;
}

int64_t Database::finalize_interrupted_transcripts() {
  std::optional<duckdb::Connection> ownedConn;
  // Own connection on purpose: transcript writes run on the session's executor/cleanup
  // thread and must not join (or race) a schedule transaction.
  ownedConn.emplace(*mDb);
  auto &conn = *ownedConn;
  auto result = executePrepared(conn, constance::kFinalizeInterruptedTranscriptsQuery,
                                {db_utils::toDuckTimestamp(nowMs() * 1000)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to finalize interrupted transcripts: "
               << (result ? result->GetError() : "prepare failed");
    return 0;
  }
  int64_t changed = 0;
  while (auto chunk = result->Fetch()) changed += static_cast<int64_t>(chunk->size());
  return changed;
}

bool Database::delete_transcript(const int64_t id) {
  if (id <= 0) return false;
  std::optional<duckdb::Connection> ownedConn;
  // Own connection on purpose: transcript writes run on the session's executor/cleanup
  // thread and must not join (or race) a schedule transaction.
  ownedConn.emplace(*mDb);
  auto &conn = *ownedConn;
  if (!transcriptExists(conn, id)) return false;
  auto links = executePrepared(conn, "DELETE FROM TranscriptClient WHERE transcript_id = $1",
                               {duckdb::Value::BIGINT(id)});
  if (!links || links->HasError()) return false;
  auto phrases = executePrepared(conn, constance::kDeletePhrasesByTranscriptIdQuery,
                                 {duckdb::Value::BIGINT(id)});
  if (!phrases || phrases->HasError()) {
    PLOG_ERROR << "Failed to delete transcript phrases (transcript_id=" << id
               << "): " << (phrases ? phrases->GetError() : "prepare failed");
    return false;
  }
  auto result = executePrepared(conn, constance::kDeleteTranscriptByIdQuery,
                                {duckdb::Value::BIGINT(id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to delete transcript (id=" << id
               << "): " << (result ? result->GetError() : "prepare failed");
    return false;
  }
  checkpointBestEffort(conn, "delete_transcript");
  return true;
}

bool Database::delete_all_transcripts() {
  std::optional<duckdb::Connection> ownedConn;
  // Own connection on purpose: transcript writes run on the session's executor/cleanup
  // thread and must not join (or race) a schedule transaction.
  ownedConn.emplace(*mDb);
  auto &conn = *ownedConn;
  if (conn.Query("DELETE FROM TranscriptClient")->HasError()) return false;
  auto phrases = executePrepared(conn, constance::kDeleteAllTranscriptPhrasesQuery, {});
  if (!phrases || phrases->HasError()) {
    PLOG_ERROR << "Failed to delete all transcript phrases: "
               << (phrases ? phrases->GetError() : "prepare failed");
    return false;
  }
  auto result = executePrepared(conn, constance::kDeleteAllTranscriptsQuery, {});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to delete all transcripts: "
               << (result ? result->GetError() : "prepare failed");
    return false;
  }
  checkpointBestEffort(conn, "delete_all_transcripts");
  return true;
}

int64_t Database::add_transcript_phrase(const DuckTranscriptPhrase &phrase) {
  if (phrase.transcript_id <= 0 || phrase.text.empty() || phrase.end_ms < phrase.start_ms) {
    PLOG_WARNING << "Rejected invalid transcript phrase (transcript_id="
                 << phrase.transcript_id << ")";
    return 0;
  }
  std::optional<duckdb::Connection> ownedConn;
  // Own connection on purpose: phrases are written from a dedicated writer thread and
  // must not join (or race) a schedule transaction.
  ownedConn.emplace(*mDb);
  auto &conn = *ownedConn;
  {
    auto state = executePrepared(conn, constance::kSelectTranscriptWriteStateQuery,
                                 {duckdb::Value::BIGINT(phrase.transcript_id)});
    if (!state || state->HasError()) return 0;
    auto state_chunk = state->Fetch();
    if (!state_chunk || state_chunk->size() == 0) {
      PLOG_WARNING << "Rejected phrase for unknown transcript (transcript_id="
                   << phrase.transcript_id << ")";
      return 0;
    }
    const auto revoked = db_utils::toBool(state_chunk->GetValue(1, 0));
    if (state_chunk->GetValue(0, 0).ToString() != "recording" || revoked) {
      PLOG_WARNING << "Rejected phrase for closed or revoked transcript (transcript_id="
                   << phrase.transcript_id << ")";
      return 0;
    }
  }
  auto result = executePrepared(
      conn, constance::kInsertTranscriptPhraseQuery,
      {duckdb::Value::BIGINT(phrase.transcript_id), duckdb::Value(phrase.track_role),
       db_utils::toDuckValue(phrase.speaker_name), duckdb::Value::BIGINT(phrase.start_ms),
       duckdb::Value::BIGINT(phrase.end_ms), duckdb::Value(phrase.text)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to insert transcript phrase (transcript_id=" << phrase.transcript_id
               << "): " << (result ? result->GetError() : "prepare failed");
    return 0;
  }
  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    PLOG_ERROR << "Empty result from RETURNING id in add_transcript_phrase";
    return 0;
  }
  return static_cast<int64_t>(chunk->GetValue(0, 0).GetValue<int32_t>());
}

std::vector<DuckTranscriptPhrase> Database::get_transcript_phrases(const int64_t transcript_id) {
  std::vector<DuckTranscriptPhrase> out;
  if (transcript_id <= 0) return out;
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(conn, constance::kSelectTranscriptPhrasesQuery,
                                {duckdb::Value::BIGINT(transcript_id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to list transcript phrases (transcript_id=" << transcript_id
               << "): " << (result ? result->GetError() : "prepare failed");
    return out;
  }
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) out.emplace_back(*chunk, i);
  }
  return out;
}

bool Database::update_transcript_phrase_text(const int64_t phrase_id, const std::string &text) {
  if (phrase_id <= 0 || text.empty()) return false;
  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  auto owner = executePrepared(conn, constance::kSelectTranscriptIdOfPhraseQuery,
                               {duckdb::Value::BIGINT(phrase_id)});
  if (!owner || owner->HasError()) return false;
  auto owner_chunk = owner->Fetch();
  if (!owner_chunk || owner_chunk->size() == 0) return false;
  const auto transcript_id =
      static_cast<int64_t>(owner_chunk->GetValue(0, 0).GetValue<int32_t>());

  auto result = executePrepared(conn, constance::kUpdateTranscriptPhraseTextQuery,
                                {duckdb::Value(text), duckdb::Value::BIGINT(phrase_id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to update transcript phrase (id=" << phrase_id
               << "): " << (result ? result->GetError() : "prepare failed");
    return false;
  }
  auto touch = executePrepared(conn, constance::kTouchTranscriptQuery,
                               {db_utils::toDuckTimestamp(nowMs() * 1000),
                                duckdb::Value::BIGINT(transcript_id)});
  if (!touch || touch->HasError()) {
    PLOG_ERROR << "Failed to bump transcript updated_at (id=" << transcript_id << ")";
    return false;
  }
  return true;
}

bool Database::delete_transcript_phrase(const int64_t phrase_id) {
  if (phrase_id <= 0) return false;
  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  auto owner = executePrepared(conn, constance::kSelectTranscriptIdOfPhraseQuery,
                               {duckdb::Value::BIGINT(phrase_id)});
  if (!owner || owner->HasError()) return false;
  auto owner_chunk = owner->Fetch();
  if (!owner_chunk || owner_chunk->size() == 0) return false;
  const auto transcript_id =
      static_cast<int64_t>(owner_chunk->GetValue(0, 0).GetValue<int32_t>());
  auto result = executePrepared(conn, constance::kDeleteTranscriptPhraseByIdQuery,
                                {duckdb::Value::BIGINT(phrase_id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to delete transcript phrase (id=" << phrase_id
               << "): " << (result ? result->GetError() : "prepare failed");
    return false;
  }
  auto touch = executePrepared(conn, constance::kTouchTranscriptQuery,
                               {db_utils::toDuckTimestamp(nowMs() * 1000),
                                duckdb::Value::BIGINT(transcript_id)});
  if (!touch || touch->HasError()) {
    PLOG_ERROR << "Failed to bump transcript updated_at (id=" << transcript_id << ")";
    return false;
  }
  return true;
}

int64_t Database::purge_orphan_transcripts() {
  std::optional<duckdb::Connection> ownedConn;
  // Own connection on purpose: transcript writes run on the session's executor/cleanup
  // thread and must not join (or race) a schedule transaction.
  ownedConn.emplace(*mDb);
  auto &conn = *ownedConn;
  if (conn.Query("DELETE FROM TranscriptClient WHERE transcript_id NOT IN (SELECT id FROM Transcript) "
                 "OR client_id NOT IN (SELECT id FROM Client)")->HasError()) return 0;
  auto phrases = executePrepared(conn, constance::kPurgeOrphanTranscriptPhrasesQuery, {});
  if (!phrases || phrases->HasError()) {
    PLOG_ERROR << "Failed to purge orphan transcript phrases: "
               << (phrases ? phrases->GetError() : "prepare failed");
    return 0;
  }
  auto result = executePrepared(conn, constance::kPurgeOrphanTranscriptsQuery, {});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to purge orphan transcripts: "
               << (result ? result->GetError() : "prepare failed");
    return 0;
  }
  int64_t removed = 0;
  while (auto chunk = result->Fetch()) removed += static_cast<int64_t>(chunk->size());
  if (removed > 0) PLOG_WARNING << "Detached missing transcript events (count=" << removed << ")";
  return removed;
}

std::vector<DuckTranscript> Database::get_transcripts_for_client(const int64_t client_id) {
  std::vector<DuckTranscript> out;
  if (client_id <= 0) return out;
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(conn, constance::kSelectTranscriptsByClientQuery,
                                {duckdb::Value::BIGINT(client_id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to list transcripts (client_id=" << client_id
               << "): " << (result ? result->GetError() : "prepare failed");
    return out;
  }
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) out.emplace_back(*chunk, i);
  }
  return out;
}

std::vector<DuckTranscript> Database::get_transcripts() {
  duckdb::Connection conn(*mDb);
  auto result = conn.Query("SELECT id, event_id, status, consent_scope, consent_given_at, "
                           "consent_revoked_at, model_id, created_at, updated_at FROM Transcript "
                           "ORDER BY created_at DESC, id DESC");
  if (!result || result->HasError()) throw std::runtime_error("Cannot list transcripts");
  std::vector<DuckTranscript> rows;
  while (auto chunk = result->Fetch())
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) rows.emplace_back(*chunk, i);
  return rows;
}

std::vector<int64_t> Database::get_transcript_client_ids(const int64_t id) {
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(conn,
      "SELECT tc.client_id FROM TranscriptClient tc JOIN Client c ON c.id = tc.client_id "
      "WHERE tc.transcript_id = $1 ORDER BY tc.client_id", {duckdb::Value::BIGINT(id)});
  if (!result || result->HasError()) throw std::runtime_error("Cannot read transcript clients");
  std::vector<int64_t> ids;
  while (auto chunk = result->Fetch())
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i)
      ids.push_back(db_utils::toInt32AsInt64(chunk->GetValue(0, i)));
  return ids;
}

bool Database::set_transcript_clients(const int64_t id, const std::vector<int64_t> &client_ids) {
  duckdb::Connection conn(*mDb);
  Transaction tx(conn);
  if (!tx.active() || !transcriptExists(conn, id)) return false;
  auto state = executePrepared(conn, constance::kSelectTranscriptWriteStateQuery,
                               {duckdb::Value::BIGINT(id)});
  auto chunk = state ? state->Fetch() : nullptr;
  if (!chunk || chunk->size() == 0 || chunk->GetValue(0, 0).ToString() == "recording") return false;
  std::set<int64_t> ids(client_ids.begin(), client_ids.end());
  for (const auto client : ids) {
    if (client <= 0) return false;
    auto result = executePrepared(conn, "SELECT 1 FROM Client WHERE id = $1",
                                  {duckdb::Value::BIGINT(client)});
    if (!result || result->HasError()) return false;
    auto found = result->Fetch();
    if (!found || found->size() == 0) return false;
  }
  auto removed = executePrepared(conn, "DELETE FROM TranscriptClient WHERE transcript_id = $1",
                                 {duckdb::Value::BIGINT(id)});
  if (!removed || removed->HasError()) return false;
  for (const auto client : ids) {
    auto inserted = executePrepared(conn, "INSERT INTO TranscriptClient VALUES ($1, $2)",
        {duckdb::Value::BIGINT(id), duckdb::Value::BIGINT(client)});
    if (!inserted || inserted->HasError()) return false;
  }
  auto touched = executePrepared(conn, constance::kTouchTranscriptQuery,
      {db_utils::toDuckTimestamp(nowMs() * 1000), duckdb::Value::BIGINT(id)});
  return touched && !touched->HasError() && tx.commit();
}

int64_t Database::count_transcripts() {
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(conn, constance::kCountTranscriptsQuery, {});
  if (!result || result->HasError()) return 0;
  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) return 0;
  return chunk->GetValue(0, 0).GetValue<int64_t>();
}

int64_t Database::count_transcript_phrases(const int64_t transcript_id) {
  if (transcript_id <= 0) return 0;
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(conn, constance::kCountTranscriptPhrasesQuery,
                                {duckdb::Value::BIGINT(transcript_id)});
  if (!result || result->HasError()) return 0;
  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) return 0;
  return chunk->GetValue(0, 0).GetValue<int64_t>();
}

bool Database::rename_transcript_speaker(const int64_t transcript_id,
                                         const std::string &track_role,
                                         const std::string &new_name) {
  if (transcript_id <= 0 || new_name.empty()) return false;
  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  if (!transcriptExists(conn, transcript_id)) return false;
  auto result = executePrepared(conn, constance::kRenameTranscriptSpeakerQuery,
                                {duckdb::Value(new_name), duckdb::Value::BIGINT(transcript_id),
                                 duckdb::Value(track_role)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to rename transcript speaker (transcript_id=" << transcript_id
               << "): " << (result ? result->GetError() : "prepare failed");
    return false;
  }
  auto touch = executePrepared(conn, constance::kTouchTranscriptQuery,
                               {db_utils::toDuckTimestamp(nowMs() * 1000),
                                duckdb::Value::BIGINT(transcript_id)});
  if (!touch || touch->HasError()) {
    PLOG_ERROR << "Failed to bump transcript updated_at (id=" << transcript_id << ")";
    return false;
  }
  return true;
}

// --- Init ---

void Database::add_demo_data() {
  duckdb::Connection conn(*mDb);
  auto result = conn.Query(constance::kDemoData);
  if (result->HasError()) {
    PLOG_ERROR << "Error inserting demo data: " << result->GetError();
  }
}

void Database::init_tables() {
  duckdb::Connection conn(*mDb);
  auto result = conn.Query(constance::kCreateTables);
  if (result->HasError()) {
    PLOG_ERROR << "Error creating tables: " << result->GetError();
  }
}

void Database::apply_schema_migrations() {
  duckdb::Connection conn(*mDb);
  auto result = conn.Query(constance::kSchemaMigrations);
  if (result->HasError()) {
    PLOG_ERROR << "Error applying schema migrations: " << result->GetError();
  }
}

void Database::init_application_metadata() {
  duckdb::Connection conn(*mDb);
  const auto nowMs = Poco::Timestamp().epochMicroseconds() / 1000;
  const auto workspaceUuid =
      Poco::UUIDGenerator::defaultGenerator().createRandom().toString();
  auto insertResult = executePrepared(
      conn, constance::kInsertApplicationMetadata,
      {duckdb::Value::INTEGER(1), duckdb::Value::INTEGER(1),
       duckdb::Value(workspaceUuid), db_utils::toDuckTimestamp(nowMs * 1000)});
  if (!insertResult || insertResult->HasError()) {
    PLOG_ERROR << "Error initializing application metadata: "
               << (insertResult ? insertResult->GetError() : "unknown error");
    throw std::runtime_error("Cannot initialize database migration metadata");
  }

  const auto metadata = get_application_metadata();
  if (metadata.schema_version == 2) return;
  if (metadata.schema_version != 1)
    throw std::runtime_error("Unsupported database schema version");
  Transaction tx(conn);
  if (!tx.active()) throw std::runtime_error("Cannot start transcript migration");
  auto migration = conn.Query(
      "ALTER TABLE Transcript ALTER COLUMN event_id DROP NOT NULL;"
      "CREATE TABLE IF NOT EXISTS TranscriptClient (transcript_id INTEGER NOT NULL, "
      "client_id INTEGER NOT NULL, PRIMARY KEY(transcript_id, client_id));"
      "INSERT INTO TranscriptClient SELECT DISTINCT t.id, ec.client_id "
      "FROM Transcript t JOIN EventClient ec ON ec.event_id = t.event_id "
      "ON CONFLICT DO NOTHING;");
  if (!migration || migration->HasError())
    throw std::runtime_error("Cannot migrate transcripts to schema 2");
  auto updateResult = executePrepared(
      conn, constance::kUpdateApplicationMetadataMigrationTime,
      {duckdb::Value::INTEGER(2), duckdb::Value::INTEGER(1),
       db_utils::toDuckTimestamp(nowMs * 1000)});
  if (!updateResult || updateResult->HasError()) {
    throw std::runtime_error("Cannot update transcript migration metadata");
  }
  if (!tx.commit()) throw std::runtime_error("Cannot commit transcript migration");
}

void Database::init_payment_status_table() {
  duckdb::Connection conn(*mDb);
  auto result = conn.Query(constance::kPaymentStatus);
  if (result->HasError()) {
    PLOG_ERROR << "Error inserting payment statuses: " << result->GetError();
  }
}

void Database::init_event_status_table() {
  duckdb::Connection conn(*mDb);
  auto result = conn.Query(constance::kEventStatus);
  if (result->HasError()) {
    PLOG_ERROR << "Error inserting event statuses: " << result->GetError();
  }
}

} // namespace pcm::database
