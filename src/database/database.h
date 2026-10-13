// app_database.h
#pragma once
#include <duckdb.hpp>
#define PLOG_NO_LOG_MACROS

#include <functional>
#include <memory>
#include <thread>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "config.h"
#include "constants.hpp"
#include "datetime.hpp"
#include "db_utils.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "plog/Initializers/RollingFileInitializer.h"
#include "schema.hpp"
#include <Poco/File.h>
#include <Poco/Timestamp.h>
#include <plog/Log.h>

namespace pcm::database {

struct ClientMonthlyStats {
  int year = 0;
  int month = 0;
  int sessions = 0;
  double income = 0.0;
};

struct DashboardSummary {
  int total_clients = 0;
  int active_clients = 0;
  int sessions_this_month = 0;
  int work_sessions_this_month = 0;
  int personal_sessions_this_month = 0;
  double income_this_month = 0.0;
};

struct DashboardMonthlyStats {
  int year = 0;
  int month = 0;
  int sessions = 0;
  int work_sessions = 0;
  int personal_sessions = 0;
  double income = 0.0;
};

// Runs the local writes of one schedule change and returns the id of the
// series they affect (nullopt on failure). Every write must go through the
// public Database methods so they share the surrounding transaction.
using ScheduleMutation = std::function<std::optional<int64_t>()>;
// Builds the wire payload from the local state visible inside the transaction.
// Returning nullopt (or throwing) aborts the whole change.
using SchedulePayloadBuilder =
    std::function<std::optional<std::string>(const ScheduleSource &)>;

class Database {
public:
  explicit Database(const pcm::config::Config &conf);

  int64_t add_event(const DuckEvent &event, bool allowOverlap = true);
  bool update_event(const DuckEvent &event, bool allowOverlap = true);
  std::unique_ptr<DuckEvent> get_event(const int64_t &id);
  bool remove_event(const int64_t &id);
  int64_t add_event_series(const DuckEventSeries &series);
  bool update_event_series(const DuckEventSeries &series);
  bool deactivate_event_series(int64_t series_id);
  bool delete_event_series_overrides_from(int64_t series_id,
                                          int64_t occurrence_start_ms);
  std::unique_ptr<DuckEventSeries> get_event_series(int64_t series_id);
  std::vector<DuckEventSeries> get_event_series_for_range(const int64_t &start_ms,
                                                          const int64_t &end_ms);
  std::vector<DuckEventSeries> get_event_series_for_client_and_range(
      int64_t client_id, int64_t range_start_ms, int64_t range_end_ms);
  std::set<std::pair<int64_t, int64_t>>
  get_event_series_exceptions_for_range(const int64_t &start_ms,
                                        const int64_t &end_ms);
  bool add_event_series_exception(int64_t series_id,
                                  int64_t occurrence_start_ms,
                                  const std::string &reason = {});

  int64_t add_client(const DuckClient &client);
  bool update_client(const DuckClient &client);
  bool remove_client(const int64_t &id);
  std::unique_ptr<DuckClient> get_client(const int64_t &id);
  std::vector<std::unique_ptr<DuckClient>> get_clients();
  std::vector<int64_t> get_client_ids();

  int64_t add_event_client(const int64_t &event_id, const int64_t &client_id);
  std::vector<DuckEvent> get_events_for_client(int64_t client_id);
  std::vector<DuckEventChangeLog> get_event_change_log_for_client(int64_t client_id);
  int64_t add_client_note(const DuckClientNote &note);
  std::vector<DuckClientNote> get_client_notes(int64_t client_id);
  int64_t add_client_note_attachment(const DuckClientNoteAttachment &attachment);
  std::vector<DuckClientNoteAttachment> get_note_attachments(int64_t note_id);

  // --- Live call transcripts (phase 2 of #118) ---
  int64_t add_transcript(std::optional<int64_t> event_id, const std::string &consent_scope,
                         const std::optional<std::string> &model_id = std::nullopt,
                         std::optional<int64_t> consent_given_at_ms = std::nullopt);
  std::unique_ptr<DuckTranscript> get_transcript(int64_t id);
  std::vector<DuckTranscript> get_transcripts_for_event(int64_t event_id);
  std::vector<DuckTranscript> get_transcripts_for_client(int64_t client_id);
  std::vector<DuckTranscript> get_transcripts();
  bool set_transcript_clients(int64_t transcript_id, const std::vector<int64_t> &client_ids);
  std::vector<int64_t> get_transcript_client_ids(int64_t transcript_id);
  int64_t count_transcripts();
  int64_t count_transcript_phrases(int64_t transcript_id);
  // Callers of set_transcript_status, revoke_transcript_consent,
  // delete_transcript and delete_all_transcripts must first stop and join the
  // transcription engine/writer: a phrase inserted between the child delete and
  // the parent delete would be left orphaned (purge_orphan_transcripts() removes it).
  bool set_transcript_status(int64_t id, const std::string &status);
  bool revoke_transcript_consent(int64_t id, std::optional<int64_t> at_ms = std::nullopt);
  // Marks transcripts left in "recording" (crash, power loss) as drafts and
  // returns how many were changed. Call once at application start, together
  // with purge_orphan_transcripts(), before any session can exist.
  int64_t finalize_interrupted_transcripts();
  // Detaches missing events, preserves standalone rows and removes dangling
  // phrases/client links. Returns the number of event links detached.
  int64_t purge_orphan_transcripts();
  // Stop and join the engine/writer first (see above). Runs a best-effort
  // CHECKPOINT afterwards so deleted text leaves the database file.
  bool delete_transcript(int64_t id);
  bool delete_all_transcripts();
  // Single writer thread invariant: ids are allocated as MAX(id)+1, so only
  // one thread may add phrases at a time. Rejects (returns 0) when the
  // transcript is not in "recording" status or its consent was revoked.
  int64_t add_transcript_phrase(const DuckTranscriptPhrase &phrase);
  std::vector<DuckTranscriptPhrase> get_transcript_phrases(int64_t transcript_id);
  bool update_transcript_phrase_text(int64_t phrase_id, const std::string &text);
  bool delete_transcript_phrase(int64_t phrase_id);
  // Sets speaker_name on every phrase of the transcript with that track role
  // (not a text edit: `edited` is untouched) and bumps updated_at. Returns true
  // iff the transcript exists, even when no phrase matched; false for an empty
  // name or an unknown transcript.
  bool rename_transcript_speaker(int64_t transcript_id, const std::string &track_role,
                                 const std::string &new_name);

  // std::vector<int64_t> get_event_ids(int64_t date);

  bool has_conflict(const DuckEvent &event);
  std::optional<DuckEvent> find_conflict(const DuckEvent &event);
  std::vector<DuckEvent> get_day_events(const int64_t &start_ms,
                                       const int64_t &end_ms);
  std::vector<DuckEvent> get_upcoming_events(const int64_t &start_ms,
                                             const int64_t &end_ms);
  bool mark_event_reminder_notified(const int64_t &event_id,
                                    const int64_t &notified_at_ms);
  std::set<std::pair<int64_t, int64_t>>
  get_notified_series_occurrences_for_range(const int64_t &start_ms,
                                            const int64_t &end_ms);
  bool mark_series_occurrence_reminder_notified(const int64_t &series_id,
                                                const int64_t &occurrence_start_ms,
                                                const int64_t &notified_at_ms);
  std::set<int64_t>
  get_materialized_occurrence_starts_for_series(const int64_t &series_id);
  std::unique_ptr<DuckEvent> get_event_by_series_occurrence(int64_t series_id,
                                                             int64_t occurrence_start_ms);
  std::vector<ClientMonthlyStats> get_client_monthly_stats(const int64_t &client_id,
                                                           int months_back = 6);
  DashboardSummary get_dashboard_summary();
  std::vector<DashboardMonthlyStats> get_dashboard_monthly_stats(int months_back = 6);
  DuckApplicationMetadata get_application_metadata() const;
  bool export_snapshot(const std::string &target_dir) const;

  DuckClient get_client_by_event(const int64_t &event_id);

  // --- Recurring schedule identity and transactional outbox ---
  //
  // Applies `mutation` and enqueues the resulting full snapshot in one
  // transaction. A series without schedule identity gets one (random UUID and
  // the explicit IANA `timezone`); with an empty timezone and no identity the
  // change is applied transactionally without an outbox entry (legacy series).
  // Nested calls are rejected. Failure of the mutation, the builder, or any
  // write rolls back everything.
  std::optional<ScheduleCommit>
  commit_schedule_change(const ScheduleMutation &mutation,
                         const std::string &timezone,
                         const SchedulePayloadBuilder &builder);
  std::optional<ScheduleIdentity> get_schedule_identity(int64_t series_id);
  std::optional<ScheduleIdentity>
  get_schedule_identity_by_uid(const std::string &series_uid);
  std::optional<ScheduleOutbox> get_schedule_outbox(const std::string &series_uid);
  // Series that still have a pending or in-flight payload, in series id order.
  std::vector<ScheduleIdentity> list_schedule_series_pending_sync();
  // Moves the pending payload into the immutable in-flight slot. Fails when a
  // payload is already in flight or the pending snapshot is not the one the
  // caller read (`expected_pending_desired`).
  bool freeze_schedule_pending(const std::string &series_uid,
                               int64_t expected_pending_desired,
                               int64_t revision, const std::string &payload,
                               const std::string &content_hash);
  // Records the server ACK of the in-flight revision. Ignored (nullopt) when
  // the revision is not the one in flight, so a late ACK never marks newer
  // local edits as synced.
  std::optional<ScheduleAck> ack_schedule_inflight(const std::string &series_uid,
                                                   int64_t revision,
                                                   const std::string &content_hash);
  // After a definitive refusal (nothing was stored server side) returns the
  // in-flight snapshot to the pending slot, in queue form, unless a newer
  // pending snapshot exists. The revision is assigned again when re-frozen.
  bool release_schedule_inflight(const std::string &series_uid,
                                 const std::string &pending_payload);
  bool set_schedule_sync_state(const std::string &series_uid,
                               const std::string &state,
                               const std::string &error = {});
  // Explicitly takes over the server's revision after a conflict (or restore)
  // and drops queued payloads; the caller re-enqueues from local state.
  // Moving the acknowledged revision backwards (server restored from an older
  // state) is refused unless the caller explicitly allows it.
  bool adopt_schedule_server_revision(const std::string &series_uid,
                                      int64_t server_revision,
                                      const std::string &content_hash,
                                      bool allow_rewind = false);
  // Final step of moving a legacy LiveKit series (one shared meeting) to a
  // published series: drops the old meeting reference, invitation state and URL
  // from the series and its materialized occurrences in one transaction and
  // returns every distinct old reference so the caller can invalidate the
  // backend meetings. The schedule itself is untouched. nullopt on failure
  // (nothing changed).
  std::optional<std::vector<std::string>> clear_series_legacy_meeting(int64_t series_id);
  std::string ensure_schedule_invitation_key(int64_t series_id);
  bool clear_schedule_invitation_key(int64_t series_id);
  bool set_schedule_invitation_generation(int64_t series_id, int64_t generation);

private:
  void add_demo_data();
  void init_tables();
  void apply_schema_migrations();
  void init_application_metadata();
  void init_payment_status_table();
  void init_event_status_table();

  duckdb::Connection &write_connection(std::optional<duckdb::Connection> &owned);
  std::optional<ScheduleIdentity> read_schedule_identity(duckdb::Connection &conn,
                                                         const char *query,
                                                         duckdb::Value key);
  bool execute_schedule_update(duckdb::Connection &conn, const char *query,
                               duckdb::vector<duckdb::Value> values);

  std::unique_ptr<duckdb::DuckDB> mDb;
  // Connection of the active schedule transaction; null outside one.
  duckdb::Connection *mTxConn = nullptr;
  // Thread that opened the active schedule transaction (the GUI thread). The
  // transaction connection must never be used by another thread, for example a
  // background backup worker; write_connection() checks this.
  std::thread::id mTxThread;
};

} // namespace pcm::database
