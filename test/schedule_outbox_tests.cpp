#include <Poco/File.h>
#include <Poco/Path.h>
#include <duckdb.hpp>
#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <stdexcept>

#include "config.h"
#include "database.h"

namespace {

using pcm::database::Database;
using pcm::database::ScheduleSource;

constexpr int64_t kStart = 1791309600000; // 2026-10-06T18:00:00Z
constexpr int64_t kHour = 3600000;

pcm::config::Config configFor(const std::string &name) {
  return pcm::config::Config{
      .db_conf = pcm::config::DatabaseConfig{
          .db_pth = Poco::Path(Poco::Path::current()).append(name)}};
}

void removeDir(const std::string &path) {
  if (Poco::File dir(path); dir.exists()) {
    dir.remove(true);
  }
}

class ScheduleOutboxTest : public ::testing::Test {
protected:
  void SetUp() override {
    mName = std::string("tmp_schedule_outbox_") +
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
    mConf = configFor(mName);
    removeDir(mConf.db_conf().db_pth.toString());
    mDb = std::make_unique<Database>(mConf);
  }
  void TearDown() override {
    mDb.reset();
    removeDir(mConf.db_conf().db_pth.toString());
  }
  void reopen() {
    mDb.reset();
    mDb = std::make_unique<Database>(mConf);
  }

  static DuckEventSeries series() {
    DuckEventSeries s;
    s.name = std::string{"Private title"};
    s.event_stat_id = 1;
    s.payment_stat_id = 1;
    s.start_date = kStart;
    s.end_date = kStart + kHour;
    s.recurrence_rule = "FREQ=WEEKLY;INTERVAL=1;BYDAY=TU";
    return s;
  }

  static pcm::database::SchedulePayloadBuilder builder(const std::string &payload) {
    return [payload](const ScheduleSource &) { return std::optional<std::string>(payload); };
  }

  std::string mName;
  pcm::config::Config mConf;
  std::unique_ptr<Database> mDb;
};

} // namespace

TEST_F(ScheduleOutboxTest, CommitsSeriesAndOutboxTogetherWithStableIdentity) {
  int64_t seriesId = 0;
  const auto commit = mDb->commit_schedule_change(
      [&]() -> std::optional<int64_t> {
        seriesId = mDb->add_event_series(series());
        return seriesId > 0 ? std::optional<int64_t>(seriesId) : std::nullopt;
      },
      "Europe/Moscow", builder("payload-1"));

  ASSERT_TRUE(commit.has_value());
  EXPECT_EQ(commit->series_id, seriesId);
  EXPECT_EQ(commit->series_uid.size(), 36u);
  EXPECT_EQ(commit->desired_revision, 1);

  const auto identity = mDb->get_schedule_identity(seriesId);
  ASSERT_TRUE(identity.has_value());
  EXPECT_EQ(identity->series_uid, commit->series_uid);
  EXPECT_EQ(identity->timezone, "Europe/Moscow");
  EXPECT_EQ(identity->acked_revision, 0);
  EXPECT_EQ(identity->desired_revision, 1);
  EXPECT_EQ(identity->sync_state, pcm::database::schedule_sync_state::kPending);

  const auto outbox = mDb->get_schedule_outbox(commit->series_uid);
  ASSERT_TRUE(outbox.has_value());
  EXPECT_EQ(outbox->pending_payload.value_or(""), "payload-1");
  EXPECT_EQ(outbox->pending_desired_revision.value_or(-1), 1);
  EXPECT_FALSE(outbox->inflight_payload.has_value());
}

TEST_F(ScheduleOutboxTest, FailedPayloadBuildWritesNeitherSeriesNorOutbox) {
  int64_t seriesId = 0;
  const auto commit = mDb->commit_schedule_change(
      [&]() -> std::optional<int64_t> {
        seriesId = mDb->add_event_series(series());
        return seriesId;
      },
      "Europe/Moscow", [](const ScheduleSource &) { return std::optional<std::string>(); });

  EXPECT_FALSE(commit.has_value());
  ASSERT_GT(seriesId, 0);
  EXPECT_EQ(mDb->get_event_series(seriesId), nullptr);
  EXPECT_FALSE(mDb->get_schedule_identity(seriesId).has_value());
  EXPECT_TRUE(mDb->list_schedule_series_pending_sync().empty());
}

TEST_F(ScheduleOutboxTest, ThrowingBuilderRollsBackEverything) {
  int64_t seriesId = 0;
  const auto commit = mDb->commit_schedule_change(
      [&]() -> std::optional<int64_t> {
        seriesId = mDb->add_event_series(series());
        return seriesId;
      },
      "Europe/Moscow",
      [](const ScheduleSource &) -> std::optional<std::string> { throw std::runtime_error("boom"); });

  EXPECT_FALSE(commit.has_value());
  EXPECT_EQ(mDb->get_event_series(seriesId), nullptr);
  EXPECT_TRUE(mDb->list_schedule_series_pending_sync().empty());
}

TEST_F(ScheduleOutboxTest, PartialMutationFailureRollsBackEarlierWrites) {
  const auto seriesId = mDb->add_event_series(series());
  ASSERT_GT(seriesId, 0);
  const auto first = mDb->commit_schedule_change(
      [&]() -> std::optional<int64_t> { return seriesId; }, "Europe/Moscow", builder("p1"));
  ASSERT_TRUE(first.has_value());

  const auto failed = mDb->commit_schedule_change(
      [&]() -> std::optional<int64_t> {
        EXPECT_TRUE(mDb->add_event_series_exception(seriesId, kStart + 7 * 24 * kHour, "deleted"));
        return std::nullopt; // later step failed
      },
      "", builder("p2"));

  EXPECT_FALSE(failed.has_value());
  EXPECT_TRUE(mDb->get_event_series_exceptions_for_range(0, kStart * 2).empty());
  const auto outbox = mDb->get_schedule_outbox(first->series_uid);
  ASSERT_TRUE(outbox.has_value());
  EXPECT_EQ(outbox->pending_payload.value_or(""), "p1");
  EXPECT_EQ(mDb->get_schedule_identity(seriesId)->desired_revision, 1);
}

TEST_F(ScheduleOutboxTest, NestedScheduleTransactionIsRejected) {
  std::optional<pcm::database::ScheduleCommit> nested;
  const auto commit = mDb->commit_schedule_change(
      [&]() -> std::optional<int64_t> {
        const auto id = mDb->add_event_series(series());
        nested = mDb->commit_schedule_change([&]() -> std::optional<int64_t> { return id; },
                                             "Europe/Moscow", builder("nested"));
        return id;
      },
      "Europe/Moscow", builder("outer"));

  EXPECT_FALSE(nested.has_value());
  ASSERT_TRUE(commit.has_value());
  EXPECT_EQ(mDb->get_schedule_outbox(commit->series_uid)->pending_payload.value_or(""), "outer");
}

TEST_F(ScheduleOutboxTest, UuidAndQueuedPayloadSurviveReopening) {
  const auto commit = mDb->commit_schedule_change(
      [&]() -> std::optional<int64_t> { return mDb->add_event_series(series()); },
      "Europe/Moscow", builder("durable"));
  ASSERT_TRUE(commit.has_value());

  reopen();

  const auto identity = mDb->get_schedule_identity(commit->series_id);
  ASSERT_TRUE(identity.has_value());
  EXPECT_EQ(identity->series_uid, commit->series_uid);
  EXPECT_EQ(identity->timezone, "Europe/Moscow");
  const auto outbox = mDb->get_schedule_outbox(commit->series_uid);
  ASSERT_TRUE(outbox.has_value());
  EXPECT_EQ(outbox->pending_payload.value_or(""), "durable");
  ASSERT_EQ(mDb->list_schedule_series_pending_sync().size(), 1u);
}

TEST_F(ScheduleOutboxTest, OverrideCancelAndDeactivateAreQueuedWithTheirLocalWrites) {
  const auto commit = mDb->commit_schedule_change(
      [&]() -> std::optional<int64_t> { return mDb->add_event_series(series()); },
      "Europe/Moscow", builder("create"));
  ASSERT_TRUE(commit.has_value());
  const auto seriesId = commit->series_id;
  const auto occurrence = kStart + 7 * 24 * kHour;

  ScheduleSource seen;
  const auto capture = [&seen](const ScheduleSource &source) {
    seen = source;
    return std::optional<std::string>("snapshot");
  };

  DuckEvent moved;
  moved.series_id = seriesId;
  moved.original_occurrence_start = occurrence;
  moved.start_date = occurrence + 24 * kHour;
  moved.end_date = occurrence + 25 * kHour;
  moved.event_stat_id = 1;
  moved.payment_stat_id = 1;
  ASSERT_TRUE(mDb->commit_schedule_change(
                     [&]() -> std::optional<int64_t> {
                       return mDb->add_event(moved) > 0 ? std::optional<int64_t>(seriesId)
                                                         : std::nullopt;
                     },
                     "", capture)
                  .has_value());
  ASSERT_EQ(seen.overrides.size(), 1u);
  EXPECT_EQ(seen.overrides[0].original_start_ms, occurrence);
  EXPECT_EQ(seen.overrides[0].start_ms, occurrence + 24 * kHour);
  EXPECT_EQ(seen.overrides[0].end_ms, occurrence + 25 * kHour);
  EXPECT_EQ(seen.overrides[0].event_stat_id, 1);
  EXPECT_EQ(seen.identity.desired_revision, 2);

  const auto cancelAt = kStart + 14 * 24 * kHour;
  ASSERT_TRUE(mDb->commit_schedule_change(
                     [&]() -> std::optional<int64_t> {
                       return mDb->add_event_series_exception(seriesId, cancelAt, "deleted")
                                  ? std::optional<int64_t>(seriesId)
                                  : std::nullopt;
                     },
                     "", capture)
                  .has_value());
  ASSERT_EQ(seen.exceptions.size(), 1u);
  EXPECT_EQ(seen.exceptions[0], cancelAt);

  ASSERT_TRUE(mDb->commit_schedule_change(
                     [&]() -> std::optional<int64_t> {
                       return mDb->deactivate_event_series(seriesId)
                                  ? std::optional<int64_t>(seriesId)
                                  : std::nullopt;
                     },
                     "", capture)
                  .has_value());
  EXPECT_FALSE(seen.series.active);
  EXPECT_EQ(seen.identity.desired_revision, 4);
  EXPECT_EQ(mDb->get_schedule_outbox(commit->series_uid)->pending_desired_revision.value_or(-1), 4);
}

TEST_F(ScheduleOutboxTest, LegacySeriesWithoutTimezoneIsChangedTransactionallyWithoutOutbox) {
  const auto seriesId = mDb->add_event_series(series());
  ASSERT_GT(seriesId, 0);

  const auto commit = mDb->commit_schedule_change(
      [&]() -> std::optional<int64_t> {
        return mDb->deactivate_event_series(seriesId) ? std::optional<int64_t>(seriesId)
                                                      : std::nullopt;
      },
      "", builder("never"));

  ASSERT_TRUE(commit.has_value());
  EXPECT_TRUE(commit->series_uid.empty());
  EXPECT_FALSE(mDb->get_schedule_identity(seriesId).has_value());
  EXPECT_TRUE(mDb->list_schedule_series_pending_sync().empty());
}

TEST_F(ScheduleOutboxTest, FreezeKeepsNewerEditPendingAndAckOfOldRevisionDoesNotSyncIt) {
  const auto commit = mDb->commit_schedule_change(
      [&]() -> std::optional<int64_t> { return mDb->add_event_series(series()); },
      "Europe/Moscow", builder("v1"));
  ASSERT_TRUE(commit.has_value());
  const auto uid = commit->series_uid;

  ASSERT_TRUE(mDb->freeze_schedule_pending(uid, 1, 1, "wire-1", "hash-1"));
  auto outbox = mDb->get_schedule_outbox(uid);
  EXPECT_EQ(outbox->inflight_payload.value_or(""), "wire-1");
  EXPECT_EQ(outbox->inflight_revision.value_or(-1), 1);
  EXPECT_FALSE(outbox->pending_payload.has_value());

  // New edit while request is in flight.
  ASSERT_TRUE(mDb->commit_schedule_change(
                     [&]() -> std::optional<int64_t> { return commit->series_id; }, "",
                     builder("v2"))
                  .has_value());
  outbox = mDb->get_schedule_outbox(uid);
  EXPECT_EQ(outbox->inflight_payload.value_or(""), "wire-1"); // immutable
  EXPECT_EQ(outbox->pending_payload.value_or(""), "v2");

  // Cannot freeze a second payload while one is in flight.
  EXPECT_FALSE(mDb->freeze_schedule_pending(uid, 2, 2, "wire-2", "hash-2"));

  const auto ack = mDb->ack_schedule_inflight(uid, 1, "hash-1");
  ASSERT_TRUE(ack.has_value());
  EXPECT_TRUE(ack->has_newer_pending);
  const auto identity = mDb->get_schedule_identity(commit->series_id);
  EXPECT_EQ(identity->acked_revision, 1);
  EXPECT_EQ(identity->desired_revision, 2);
  EXPECT_EQ(identity->sync_state, pcm::database::schedule_sync_state::kPending);
  EXPECT_EQ(mDb->get_schedule_outbox(uid)->pending_payload.value_or(""), "v2");

  // Stale/duplicate ack for an unknown revision is ignored.
  EXPECT_FALSE(mDb->ack_schedule_inflight(uid, 1, "hash-1").has_value());

  ASSERT_TRUE(mDb->freeze_schedule_pending(uid, 2, 2, "wire-2", "hash-2"));
  const auto second = mDb->ack_schedule_inflight(uid, 2, "hash-2");
  ASSERT_TRUE(second.has_value());
  EXPECT_FALSE(second->has_newer_pending);
  EXPECT_EQ(mDb->get_schedule_identity(commit->series_id)->sync_state,
            pcm::database::schedule_sync_state::kSynced);
  EXPECT_TRUE(mDb->list_schedule_series_pending_sync().empty());
}

TEST_F(ScheduleOutboxTest, FreezeRejectsStalePendingSnapshot) {
  const auto commit = mDb->commit_schedule_change(
      [&]() -> std::optional<int64_t> { return mDb->add_event_series(series()); },
      "Europe/Moscow", builder("v1"));
  ASSERT_TRUE(commit.has_value());
  ASSERT_TRUE(mDb->commit_schedule_change(
                     [&]() -> std::optional<int64_t> { return commit->series_id; }, "",
                     builder("v2"))
                  .has_value());
  // The caller read desired revision 1, but 2 replaced it meanwhile.
  EXPECT_FALSE(mDb->freeze_schedule_pending(commit->series_uid, 1, 1, "wire", "h"));
  EXPECT_FALSE(mDb->get_schedule_outbox(commit->series_uid)->inflight_payload.has_value());
}

TEST_F(ScheduleOutboxTest, ConflictStateSurvivesEditsAndReopening) {
  const auto commit = mDb->commit_schedule_change(
      [&]() -> std::optional<int64_t> { return mDb->add_event_series(series()); },
      "Europe/Moscow", builder("v1"));
  ASSERT_TRUE(commit.has_value());
  ASSERT_TRUE(mDb->set_schedule_sync_state(commit->series_uid,
                                           pcm::database::schedule_sync_state::kConflict,
                                           "revision_conflict"));
  ASSERT_TRUE(mDb->commit_schedule_change(
                     [&]() -> std::optional<int64_t> { return commit->series_id; }, "",
                     builder("v2"))
                  .has_value());
  reopen();
  const auto identity = mDb->get_schedule_identity(commit->series_id);
  EXPECT_EQ(identity->sync_state, pcm::database::schedule_sync_state::kConflict);
  EXPECT_EQ(identity->last_error, "revision_conflict");
}

TEST_F(ScheduleOutboxTest, AdoptServerRevisionRequeuesWithoutResettingRevisionBackwards) {
  const auto commit = mDb->commit_schedule_change(
      [&]() -> std::optional<int64_t> { return mDb->add_event_series(series()); },
      "Europe/Moscow", builder("v1"));
  ASSERT_TRUE(commit.has_value());
  ASSERT_TRUE(mDb->freeze_schedule_pending(commit->series_uid, 1, 1, "wire", "h"));
  ASSERT_TRUE(mDb->set_schedule_sync_state(commit->series_uid,
                                           pcm::database::schedule_sync_state::kConflict, "x"));

  ASSERT_TRUE(mDb->adopt_schedule_server_revision(commit->series_uid, 5, "server-hash"));

  const auto identity = mDb->get_schedule_identity(commit->series_id);
  EXPECT_EQ(identity->acked_revision, 5);
  EXPECT_EQ(identity->acked_content_hash, "server-hash");
  EXPECT_EQ(identity->sync_state, pcm::database::schedule_sync_state::kPending);
  const auto outbox = mDb->get_schedule_outbox(commit->series_uid);
  EXPECT_FALSE(outbox->inflight_payload.has_value());
  EXPECT_FALSE(outbox->pending_payload.has_value());
  EXPECT_FALSE(mDb->adopt_schedule_server_revision(commit->series_uid, 4, "older"));
}

TEST_F(ScheduleOutboxTest, AdoptingServerRevisionInsideCommitIsAtomicWithRequeue) {
  const auto commit = mDb->commit_schedule_change(
      [&]() -> std::optional<int64_t> { return mDb->add_event_series(series()); },
      "Europe/Moscow", builder("v1"));
  ASSERT_TRUE(commit.has_value());
  ASSERT_TRUE(mDb->set_schedule_sync_state(commit->series_uid,
                                           pcm::database::schedule_sync_state::kConflict, "x"));

  // A failing requeue must leave the conflict and the queued payload untouched.
  EXPECT_FALSE(mDb->commit_schedule_change(
                      [&]() -> std::optional<int64_t> {
                        EXPECT_TRUE(mDb->adopt_schedule_server_revision(commit->series_uid, 3, "h", true));
                        return commit->series_id;
                      },
                      "", [](const ScheduleSource &) { return std::optional<std::string>(); })
                   .has_value());
  EXPECT_EQ(mDb->get_schedule_identity(commit->series_id)->acked_revision, 0);
  EXPECT_EQ(mDb->get_schedule_identity(commit->series_id)->sync_state,
            pcm::database::schedule_sync_state::kConflict);
  EXPECT_EQ(mDb->get_schedule_outbox(commit->series_uid)->pending_payload.value_or(""), "v1");

  ASSERT_TRUE(mDb->commit_schedule_change(
                     [&]() -> std::optional<int64_t> {
                       return mDb->adopt_schedule_server_revision(commit->series_uid, 3, "h", true)
                                  ? std::optional<int64_t>(commit->series_id)
                                  : std::nullopt;
                     },
                     "", builder("rebuilt"))
                  .has_value());
  const auto identity = mDb->get_schedule_identity(commit->series_id);
  EXPECT_EQ(identity->acked_revision, 3);
  EXPECT_EQ(identity->sync_state, pcm::database::schedule_sync_state::kPending);
  EXPECT_EQ(mDb->get_schedule_outbox(commit->series_uid)->pending_payload.value_or(""), "rebuilt");
}

TEST_F(ScheduleOutboxTest, InvitationGenerationAndIdempotencyKeyArePersisted) {
  const auto commit = mDb->commit_schedule_change(
      [&]() -> std::optional<int64_t> { return mDb->add_event_series(series()); },
      "Europe/Moscow", builder("v1"));
  ASSERT_TRUE(commit.has_value());

  const auto key = mDb->ensure_schedule_invitation_key(commit->series_id);
  EXPECT_EQ(key.size(), 36u);
  EXPECT_EQ(mDb->ensure_schedule_invitation_key(commit->series_id), key);
  reopen();
  EXPECT_EQ(mDb->ensure_schedule_invitation_key(commit->series_id), key);

  ASSERT_TRUE(mDb->set_schedule_invitation_generation(commit->series_id, 3));
  ASSERT_TRUE(mDb->clear_schedule_invitation_key(commit->series_id));
  const auto identity = mDb->get_schedule_identity(commit->series_id);
  EXPECT_EQ(identity->invitation_generation, 3);
  EXPECT_FALSE(identity->invitation_key.has_value());
  EXPECT_NE(mDb->ensure_schedule_invitation_key(commit->series_id), key);
}

TEST_F(ScheduleOutboxTest, MigratesLegacyDatabaseAndRoundTripsThroughBackupSnapshot) {
  // A database created before schedule tables existed.
  mDb.reset();
  removeDir(mConf.db_conf().db_pth.toString());
  Poco::File(mConf.db_conf().db_pth.toString()).createDirectories();
  {
    duckdb::DuckDB legacy(mConf.db_conf().db_pth.toString() + "/database.db");
    duckdb::Connection conn(legacy);
    // EventSeries as shipped before provider columns and schedule tables.
    ASSERT_FALSE(conn.Query(
                         "CREATE TABLE EventSeries (id INTEGER PRIMARY KEY, name TEXT, "
                         "description TEXT, client_id INTEGER, is_work_event BOOLEAN, "
                         "event_stat_id INTEGER, payment_stat_id INTEGER, start_date TIMESTAMP, "
                         "end_date TIMESTAMP, duration INTEGER, cost DOUBLE, is_online BOOLEAN "
                         "DEFAULT FALSE, meeting_url TEXT, recurrence_rule TEXT NOT NULL, "
                         "recurrence_until TIMESTAMP, active BOOLEAN DEFAULT TRUE, created_at "
                         "TIMESTAMP, updated_at TIMESTAMP, cancellation_reason TEXT, canceled_by "
                         "TEXT, buffer_before_minutes INTEGER DEFAULT 0, buffer_after_minutes "
                         "INTEGER DEFAULT 0)")
                     ->HasError());
    ASSERT_FALSE(conn.Query("INSERT INTO EventSeries (id, recurrence_rule, start_date, end_date, "
                            "event_stat_id, payment_stat_id, is_work_event, is_online, active) "
                            "VALUES (1, 'FREQ=WEEKLY', TIMESTAMP '2026-10-06 18:00:00', "
                            "TIMESTAMP '2026-10-06 19:00:00', 1, 1, TRUE, FALSE, TRUE)")
                     ->HasError());
  }
  mDb = std::make_unique<Database>(mConf); // must migrate in place
  ASSERT_FALSE(mDb->get_schedule_identity(1).has_value());
  ASSERT_NE(mDb->get_event_series(1), nullptr); // legacy row readable after migration

  const auto commit = mDb->commit_schedule_change(
      [&]() -> std::optional<int64_t> { return mDb->add_event_series(series()); },
      "Europe/Moscow", builder("round-trip"));
  ASSERT_TRUE(commit.has_value());
  ASSERT_TRUE(mDb->freeze_schedule_pending(commit->series_uid, 1, 1, "wire-frozen", "hash"));
  ASSERT_TRUE(mDb->commit_schedule_change(
                     [&]() -> std::optional<int64_t> { return commit->series_id; }, "",
                     builder("newer"))
                  .has_value());
  ASSERT_TRUE(mDb->ack_schedule_inflight(commit->series_uid, 1, "hash").has_value());

  const auto exportDir =
      Poco::Path(Poco::Path::current()).append(mName + "_export").toString();
  removeDir(exportDir);
  ASSERT_TRUE(mDb->export_snapshot(exportDir));

  // Restore the same way RestoreService does: IMPORT into a fresh database.
  mDb.reset();
  removeDir(mConf.db_conf().db_pth.toString());
  Poco::File(mConf.db_conf().db_pth.toString()).createDirectories();
  {
    duckdb::DuckDB restored(mConf.db_conf().db_pth.toString() + "/database.db");
    duckdb::Connection conn(restored);
    auto imported = conn.Query("IMPORT DATABASE '" + exportDir + "'");
    ASSERT_FALSE(imported->HasError()) << imported->GetError();
  }
  mDb = std::make_unique<Database>(mConf);
  removeDir(exportDir);

  const auto identity = mDb->get_schedule_identity_by_uid(commit->series_uid);
  ASSERT_TRUE(identity.has_value());
  EXPECT_EQ(identity->timezone, "Europe/Moscow");
  EXPECT_EQ(identity->acked_revision, 1);
  EXPECT_EQ(identity->acked_content_hash, "hash");
  EXPECT_EQ(identity->desired_revision, 2);
  EXPECT_EQ(mDb->get_schedule_outbox(commit->series_uid)->pending_payload.value_or(""), "newer");
}
