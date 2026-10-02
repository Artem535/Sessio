#include "schedule_sync.h"

#include "config.h"
#include "database.h"
#include "fake_schedule_backend.h"
#include "schedule_snapshot.h"
#include "token_backend_client.h"

#include <Poco/File.h>
#include <Poco/Path.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <gtest/gtest.h>

#include <memory>

using namespace pcm::meeting;
using pcm::database::Database;
using pcm::tokenclient::TokenBackendClient;

namespace {

constexpr int64_t kStart = 1791309600000; // 2026-10-06T18:00:00Z
constexpr int64_t kHour = 3600000;

// Server-side CAS model matching the Task 2 contract: revision == stored
// revision with identical content is an idempotent replay, base_revision must
// equal the stored revision otherwise.
struct ModelServer {
  struct Series {
    int64_t revision = 0;
    std::string hash;
  };
  QHash<QString, Series> series;
  int puts = 0;
  bool capabilities = true;
  int forceStatusForNextPuts = 0; // when >0, answer that many PUTs with forcedStatus
  int forcedStatus = 0;
  bool applyButDropReply = false; // lost ACK: apply, then close without answering
  int putDelayMs = 0;
  bool neverRespondToPut = false;
  QList<QByteArray> putBodies;

  FakeScheduleBackend::Response handle(const FakeScheduleBackend::Request &request) {
    using R = FakeScheduleBackend::Response;
    if (request.path == "/v1/capabilities") {
      return capabilities ? R{.status = 200, .body = R"({"scheduleSeries":true})"}
                          : R{.status = 404, .body = R"({"error":"not_found"})"};
    }
    const auto uid = request.path.section('/', 3, 3);
    if (request.method == "GET") {
      if (!series.contains(uid)) {
        return {.status = 404, .body = R"({"error":"not_found"})"};
      }
      const auto &stored = series[uid];
      return {.status = 200,
              .body = QByteArray(QJsonDocument(QJsonObject{{"series_uid", uid},
                                                           {"revision", static_cast<qint64>(stored.revision)},
                                                           {"content_hash", QString::fromStdString(stored.hash)},
                                                           {"snapshot", QJsonObject{}}})
                                     .toJson(QJsonDocument::Compact))};
    }
    ++puts;
    putBodies.append(request.body);
    if (neverRespondToPut) {
      return {.neverRespond = true};
    }
    if (forceStatusForNextPuts > 0) {
      --forceStatusForNextPuts;
      return {.status = forcedStatus, .body = R"({"error":"invalid_schedule"})"};
    }
    const auto snapshot = parseSnapshot(request.body);
    if (!snapshot) {
      return {.status = 422, .body = R"({"error":"invalid_schedule"})"};
    }
    const auto hash = scheduleContentHash(*snapshot);
    auto &stored = series[uid];
    R response{.status = 200, .delayMs = putDelayMs};
    if (stored.revision == snapshot->revision && stored.revision != 0) {
      if (stored.hash != hash) {
        return {.status = 409, .body = R"({"error":"revision_conflict"})"};
      }
    } else if (stored.revision != snapshot->baseRevision ||
               snapshot->revision != snapshot->baseRevision + 1) {
      return {.status = 409, .body = R"({"error":"revision_conflict"})"};
    } else {
      stored = {snapshot->revision, hash};
    }
    if (applyButDropReply) {
      applyButDropReply = false;
      return {.dropConnection = true};
    }
    response.body = QByteArray(QJsonDocument(QJsonObject{{"series_uid", uid},
                                                         {"revision", static_cast<qint64>(stored.revision)},
                                                         {"content_hash", QString::fromStdString(stored.hash)}})
                                   .toJson(QJsonDocument::Compact));
    return response;
  }
};

class ScheduleSyncTest : public ::testing::Test {
protected:
  void SetUp() override {
    mName = std::string("tmp_schedule_sync_") +
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
    mConf = pcm::config::Config{
        .db_conf = pcm::config::DatabaseConfig{
            .db_pth = Poco::Path(Poco::Path::current()).append(mName)}};
    if (Poco::File dir(mConf.db_conf().db_pth.toString()); dir.exists()) {
      dir.remove(true);
    }
    mDb = std::make_unique<Database>(mConf);
    mBackend = std::make_unique<FakeScheduleBackend>();
    mBackend->setHandler([this](const FakeScheduleBackend::Request &r) { return mServer.handle(r); });
    makeSync(mBackend->baseUrl());
  }
  void TearDown() override {
    mSync.reset();
    mClient.reset();
    mBackend.reset();
    mDb.reset();
    if (Poco::File dir(mConf.db_conf().db_pth.toString()); dir.exists()) {
      dir.remove(true);
    }
  }

  void makeSync(const QString &baseUrl) {
    mSync.reset();
    mClient = std::make_unique<TokenBackendClient>(baseUrl);
    mSync = std::make_unique<ScheduleSync>(
        *mDb, *mClient,
        [this](const ScheduleSync::CredentialCallback &done) {
          ++credentialReads;
          done(credentialAvailable, credentialAvailable ? QStringLiteral("secret-cred") : QString());
        });
    // Fast, deterministic retries; policy shape is tested separately.
    mSync->setBackoffPolicy([](int) { return 30; });
  }

  void reopenDatabase() {
    mSync.reset();
    mDb.reset();
    mDb = std::make_unique<Database>(mConf);
  }

  static DuckEventSeries series() {
    DuckEventSeries s;
    s.name = std::string{"Private title"};
    s.description = std::string{"private notes"};
    s.event_stat_id = 1;
    s.payment_stat_id = 1;
    s.start_date = kStart;
    s.end_date = kStart + kHour;
    s.recurrence_rule = "FREQ=WEEKLY;INTERVAL=1;BYDAY=TU";
    return s;
  }

  std::optional<pcm::database::ScheduleCommit> createSeries() {
    return mDb->commit_schedule_change(
        [&]() -> std::optional<int64_t> { return mDb->add_event_series(series()); },
        "Europe/Moscow", buildScheduleSnapshotPayload);
  }

  std::optional<pcm::database::ScheduleCommit> cancelOccurrence(int64_t seriesId, int weeks) {
    return mDb->commit_schedule_change(
        [&]() -> std::optional<int64_t> {
          return mDb->add_event_series_exception(seriesId, kStart + weeks * 7 * 24 * kHour,
                                                 "deleted")
                     ? std::optional<int64_t>(seriesId)
                     : std::nullopt;
        },
        "", buildScheduleSnapshotPayload);
  }

  static bool waitFor(const std::function<bool()> &condition, int timeoutMs = 3000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
      QTest::qWait(10);
    }
    return condition();
  }

  bool waitForState(const QString &uid, ScheduleSyncState state, int timeoutMs = 3000) {
    return waitFor([&] { return mSync->status(uid).state == state; }, timeoutMs);
  }

  std::string mName;
  pcm::config::Config mConf;
  std::unique_ptr<Database> mDb;
  std::unique_ptr<FakeScheduleBackend> mBackend;
  std::unique_ptr<TokenBackendClient> mClient;
  std::unique_ptr<ScheduleSync> mSync;
  ModelServer mServer;
  bool credentialAvailable = true;
  int credentialReads = 0;
};

} // namespace

TEST_F(ScheduleSyncTest, ProbesCapabilitiesThenPublishesFullSnapshotWithoutPersonalData) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  QSignalSpy synced(mSync.get(), &ScheduleSync::synced);

  mSync->start();

  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Synced));
  ASSERT_GE(mBackend->requests().size(), 2);
  EXPECT_EQ(mBackend->requests()[0].path, "/v1/capabilities");
  EXPECT_EQ(mBackend->requests()[1].method, "PUT");
  EXPECT_EQ(mBackend->requests()[1].path, "/v1/schedule-series/" + uid);
  EXPECT_EQ(mBackend->requests()[1].headers.value("authorization"), "Bearer secret-cred");
  const auto body = QJsonDocument::fromJson(mBackend->requests()[1].body).object();
  EXPECT_EQ(body["revision"].toInt(), 1);
  EXPECT_EQ(body["base_revision"].toInt(), 0);
  EXPECT_EQ(body["timezone"].toString(), "Europe/Moscow");
  EXPECT_FALSE(mBackend->requests()[1].body.contains("Private"));
  EXPECT_FALSE(mBackend->requests()[1].body.contains("notes"));
  ASSERT_GE(synced.count(), 1);
  EXPECT_EQ(synced.last().at(0).toString(), uid);
  EXPECT_EQ(synced.last().at(1).toLongLong(), 1);
  EXPECT_EQ(mDb->get_schedule_identity(commit->series_id)->acked_revision, 1);
  EXPECT_EQ(mServer.series[uid].revision, 1);
}

TEST_F(ScheduleSyncTest, StartDoesNotBlockTheEventLoopOnSlowBackend) {
  ASSERT_TRUE(createSeries().has_value());
  mServer.putDelayMs = 600;
  QElapsedTimer timer;
  timer.start();
  mSync->start();
  EXPECT_LT(timer.elapsed(), 100);
}

TEST_F(ScheduleSyncTest, LegacyBackendIsVisiblyUnsupportedAndNeverFallsBackToSingleMeeting) {
  mServer.capabilities = false;
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  QSignalSpy capability(mSync.get(), &ScheduleSync::capabilityChanged);

  mSync->start();

  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Unsupported));
  EXPECT_EQ(mSync->capability(), ScheduleCapability::Unsupported);
  EXPECT_GE(capability.count(), 1);
  EXPECT_EQ(mServer.puts, 0);
  EXPECT_EQ(mBackend->requestCount("POST", "/v1/meetings"), 0);
  EXPECT_TRUE(mSync->status(uid).unconfirmed);
  // Local data is untouched and still queued.
  EXPECT_TRUE(mDb->get_schedule_outbox(commit->series_uid)->pending_payload.has_value());

  // After the backend is upgraded, an explicit wake publishes the queue.
  mServer.capabilities = true;
  mSync->wake();
  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Synced));
  EXPECT_EQ(mServer.puts, 1);
}

TEST_F(ScheduleSyncTest, LostAckResendsIdenticalRevisionAndContent) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  mServer.applyButDropReply = true;

  mSync->start();

  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Synced));
  ASSERT_EQ(mServer.putBodies.size(), 2);
  EXPECT_EQ(mServer.putBodies[0], mServer.putBodies[1]); // byte-identical replay
  EXPECT_EQ(mServer.series[uid].revision, 1);
  EXPECT_EQ(mDb->get_schedule_identity(commit->series_id)->acked_revision, 1);
}

TEST_F(ScheduleSyncTest, EditDuringInFlightRequestIsNotMarkedSyncedByTheOldAck) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  mServer.putDelayMs = 250;

  mSync->start();
  ASSERT_TRUE(waitFor([&] { return mServer.puts >= 1; }));
  ASSERT_TRUE(cancelOccurrence(commit->series_id, 1).has_value());
  mSync->notifyLocalChange(uid);

  // The first request completes; its ACK must leave the newer edit queued.
  ASSERT_TRUE(waitFor([&] { return mDb->get_schedule_identity(commit->series_id)->acked_revision == 1; }));
  EXPECT_NE(mSync->status(uid).state, ScheduleSyncState::Synced);
  EXPECT_TRUE(mSync->status(uid).unconfirmed);

  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Synced));
  ASSERT_EQ(mServer.putBodies.size(), 2);
  const auto first = QJsonDocument::fromJson(mServer.putBodies[0]).object();
  const auto second = QJsonDocument::fromJson(mServer.putBodies[1]).object();
  EXPECT_EQ(first["exceptions"].toArray().size(), 0); // immutable in-flight payload
  EXPECT_EQ(second["revision"].toInt(), 2);
  EXPECT_EQ(second["base_revision"].toInt(), 1);
  EXPECT_EQ(second["exceptions"].toArray().size(), 1);
  EXPECT_EQ(mServer.series[uid].revision, 2);
}

TEST_F(ScheduleSyncTest, OfflineFromTheStartWaitsAndPublishesAfterRestartOnline) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);

  makeSync("http://127.0.0.1:1"); // nothing listens: offline
  mSync->start();
  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::WaitingForNetwork, 5000));
  EXPECT_TRUE(mSync->status(uid).unconfirmed);
  EXPECT_EQ(mSync->capability(), ScheduleCapability::Unreachable);

  reopenDatabase(); // app restart
  makeSync(mBackend->baseUrl());
  mSync->start();

  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Synced, 5000));
  EXPECT_EQ(mServer.puts, 1);
}

TEST_F(ScheduleSyncTest, InFlightPayloadSurvivesRestartAndIsResentByteIdentical) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  mClient->setScheduleRequestTimeoutMs(200);
  mServer.neverRespondToPut = true;

  mSync->start();
  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::WaitingForNetwork, 3000));
  const auto frozen = mDb->get_schedule_outbox(commit->series_uid)->inflight_payload;
  ASSERT_TRUE(frozen.has_value());

  reopenDatabase(); // app restart while the request was unanswered
  makeSync(mBackend->baseUrl());
  mServer.neverRespondToPut = false;
  mSync->start();

  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Synced, 5000));
  ASSERT_EQ(mServer.putBodies.size(), 2);
  EXPECT_EQ(mServer.putBodies[0].toStdString(), *frozen);
  EXPECT_EQ(mServer.putBodies[1].toStdString(), *frozen);
}

TEST_F(ScheduleSyncTest, ConflictPausesTheSeriesAndStaysPausedAcrossRestart) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  mServer.series[uid] = {5, "other-device-hash"}; // someone else published

  mSync->start();

  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Conflict));
  const auto putsAtConflict = mServer.puts;
  QTest::qWait(200);
  EXPECT_EQ(mServer.puts, putsAtConflict); // no automatic retry
  EXPECT_EQ(mDb->get_schedule_identity(commit->series_id)->sync_state,
            pcm::database::schedule_sync_state::kConflict);

  reopenDatabase();
  makeSync(mBackend->baseUrl());
  mSync->start();
  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Conflict));
  QTest::qWait(200);
  EXPECT_EQ(mServer.puts, putsAtConflict);
  EXPECT_EQ(mServer.series[uid].revision, 5); // server never overwritten
}

TEST_F(ScheduleSyncTest, RestoredOldBackupConflictIsOnlyOverwrittenByExplicitPublish) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  mSync->start();
  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Synced)); // acked revision 1

  // Server moved on (revision 3) after the backup this database represents.
  mServer.series[uid] = {3, "newer-hash"};
  ASSERT_TRUE(cancelOccurrence(commit->series_id, 2).has_value());
  mSync->notifyLocalChange(uid);
  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Conflict));
  EXPECT_EQ(mServer.series[uid].revision, 3);

  QSignalSpy fetched(mSync.get(), &ScheduleSync::serverSnapshotFetched);
  mSync->fetchServerSnapshot(uid);
  ASSERT_TRUE(fetched.wait(2000));
  EXPECT_EQ(fetched.at(0).at(0).toString(), uid);
  EXPECT_EQ(fetched.at(0).at(1).toLongLong(), 3);
  EXPECT_EQ(mServer.series[uid].hash, "newer-hash"); // viewing changes nothing

  mSync->publishRestoredSchedule(uid);
  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Synced));
  EXPECT_EQ(mServer.series[uid].revision, 4);
  const auto last = QJsonDocument::fromJson(mServer.putBodies.last()).object();
  EXPECT_EQ(last["base_revision"].toInt(), 3);
  EXPECT_EQ(last["exceptions"].toArray().size(), 1);
}

TEST_F(ScheduleSyncTest, ValidationRejectionStopsAutomaticRetriesUntilManualRetryOrEdit) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  mServer.forceStatusForNextPuts = 1;
  mServer.forcedStatus = 422;

  mSync->start();
  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Rejected));
  QTest::qWait(200);
  EXPECT_EQ(mServer.puts, 1);
  EXPECT_FALSE(mSync->status(uid).detail.isEmpty());
  EXPECT_TRUE(mDb->get_schedule_outbox(commit->series_uid)->inflight_payload.has_value()); // kept

  mSync->retry(uid);
  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Synced));
  EXPECT_EQ(mServer.puts, 2);
}

TEST_F(ScheduleSyncTest, ServerErrorsAndTimeoutsBackOffAndRetrySameBytes) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  mServer.forceStatusForNextPuts = 2;
  mServer.forcedStatus = 503;

  mSync->start();

  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Synced));
  ASSERT_EQ(mServer.putBodies.size(), 3);
  EXPECT_EQ(mServer.putBodies[0], mServer.putBodies[1]);
  EXPECT_EQ(mServer.putBodies[1], mServer.putBodies[2]);
}

TEST_F(ScheduleSyncTest, RequestTimeoutIsBoundedAndRetried) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  mClient->setScheduleRequestTimeoutMs(200);
  mServer.neverRespondToPut = true;

  mSync->start();
  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::WaitingForNetwork, 3000));
  mServer.neverRespondToPut = false;
  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Synced, 3000));
}

TEST_F(ScheduleSyncTest, RateLimitHonorsRetryAfter) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  int puts = 0;
  mBackend->setHandler([&](const FakeScheduleBackend::Request &r) {
    if (r.method == "PUT" && puts++ == 0) {
      return FakeScheduleBackend::Response{.status = 429,
                                           .body = R"({"error":"too_many_attempts"})",
                                           .headers = {{"Retry-After", "1"}}};
    }
    return mServer.handle(r);
  });

  mSync->start();
  ASSERT_TRUE(waitFor([&] { return puts >= 1; }));
  QTest::qWait(400);
  EXPECT_EQ(puts, 1); // 30 ms policy would have retried; Retry-After wins
  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Synced, 3000));
  EXPECT_EQ(puts, 2);
}

TEST_F(ScheduleSyncTest, UnavailableCredentialIsReportedAndRetriedLater) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  credentialAvailable = false;

  mSync->start();
  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::CredentialUnavailable));
  EXPECT_EQ(mServer.puts, 0);
  EXPECT_EQ(mBackend->requests().size(), 0); // nothing sent without a credential

  credentialAvailable = true;
  mSync->wake();
  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Synced));
}

TEST_F(ScheduleSyncTest, UnauthorizedStopsUntilWake) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  bool authorized = false;
  mBackend->setHandler([&](const FakeScheduleBackend::Request &r) {
    if (!authorized) {
      return FakeScheduleBackend::Response{.status = 401, .body = R"({"error":"unauthorized"})"};
    }
    return mServer.handle(r);
  });

  mSync->start();
  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Unauthorized));
  const auto count = mBackend->requests().size();
  QTest::qWait(200);
  EXPECT_EQ(mBackend->requests().size(), count);

  authorized = true;
  mSync->wake();
  ASSERT_TRUE(waitForState(uid, ScheduleSyncState::Synced));
}

TEST_F(ScheduleSyncTest, ConcurrentSeriesAreCorrelatedByUuid) {
  const auto first = createSeries();
  const auto second = createSeries();
  ASSERT_TRUE(first && second);
  const auto uid1 = QString::fromStdString(first->series_uid);
  const auto uid2 = QString::fromStdString(second->series_uid);
  mServer.series[uid2] = {7, "foreign"}; // second series conflicts, first is fine

  mSync->start();

  ASSERT_TRUE(waitForState(uid1, ScheduleSyncState::Synced));
  ASSERT_TRUE(waitForState(uid2, ScheduleSyncState::Conflict));
  EXPECT_EQ(mSync->status(uid1).ackedRevision, 1);
  EXPECT_EQ(mSync->status(uid2).ackedRevision, 0);
}

TEST_F(ScheduleSyncTest, BackoffPolicyIsBoundedWithJitter) {
  const int base[] = {2000, 5000, 15000, 60000, 300000, 300000, 300000};
  for (int attempt = 0; attempt < 7; ++attempt) {
    for (const double unit : {0.0, 0.5, 0.999}) {
      const auto delay = ScheduleSync::retryDelayMs(attempt, unit);
      EXPECT_GE(delay, static_cast<int>(base[attempt] * 0.8)) << attempt;
      EXPECT_LE(delay, 300000) << attempt;
      EXPECT_LE(delay, static_cast<int>(base[attempt] * 1.2)) << attempt;
    }
  }
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
