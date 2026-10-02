#include "series_invitation_service.h"

#include "config.h"
#include "database.h"
#include "fake_series_invitation_store.h"
#include "schedule_snapshot.h"
#include "series_backend_model.h"
#include "series_join_target.h"
#include "token_backend_client.h"

#include <Poco/File.h>
#include <Poco/Path.h>
#include <QCoreApplication>
#include <QElapsedTimer>
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

class SeriesInvitationTest : public ::testing::Test {
protected:
  void SetUp() override {
    mName = std::string("tmp_series_invitation_") +
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
    mConf = pcm::config::Config{
        .db_conf = pcm::config::DatabaseConfig{
            .db_pth = Poco::Path(Poco::Path::current()).append(mName)}};
    if (Poco::File dir(mConf.db_conf().db_pth.toString()); dir.exists()) {
      dir.remove(true);
    }
    mDb = std::make_unique<Database>(mConf);
    mBackend = std::make_unique<FakeScheduleBackend>();
    mBackend->setHandler([this](const FakeScheduleBackend::Request &r) { return mModel.handle(r); });
    mClient = std::make_unique<TokenBackendClient>(mBackend->baseUrl());
    mStore = std::make_unique<FakeSeriesInvitationStore>();
    mSync = std::make_unique<ScheduleSync>(*mDb, *mClient, reader());
    mSync->setBackoffPolicy([](int) { return 30; });
    mService = std::make_unique<SeriesInvitationService>(*mDb, *mSync, *mClient, reader(), *mStore);
    mService->setBackoffPolicy([](int) { return 30; });
  }
  void TearDown() override {
    mService.reset();
    mSync.reset();
    mClient.reset();
    mBackend.reset();
    mDb.reset();
    if (Poco::File dir(mConf.db_conf().db_pth.toString()); dir.exists()) {
      dir.remove(true);
    }
  }

  ScheduleSync::CredentialReader reader() {
    return [this](const ScheduleSync::CredentialCallback &done) {
      ++credentialReads;
      done(credentialAvailable, credentialAvailable ? QStringLiteral("secret-cred") : QString());
    };
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

  bool waitForInvitation(int64_t seriesId, InvitationState state, int timeoutMs = 3000) {
    return waitFor([&] { return mService->status(seriesId).state == state; }, timeoutMs);
  }

  std::string mName;
  pcm::config::Config mConf;
  std::unique_ptr<Database> mDb;
  std::unique_ptr<FakeScheduleBackend> mBackend;
  std::unique_ptr<TokenBackendClient> mClient;
  std::unique_ptr<FakeSeriesInvitationStore> mStore;
  std::unique_ptr<ScheduleSync> mSync;
  std::unique_ptr<SeriesInvitationService> mService;
  SeriesBackendModel mModel;
  bool credentialAvailable = true;
  int credentialReads = 0;
};

} // namespace

TEST_F(SeriesInvitationTest, NoInvitationIsRequestedUntilTheScheduleIsAcknowledged) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  mClient->setScheduleRequestTimeoutMs(300);
  mModel.neverRespondToPut = true; // the schedule never gets an ACK
  mSync->start();
  ASSERT_TRUE(waitFor([&] { return !mModel.putBodies.isEmpty(); }));

  mService->ensureInvitation(commit->series_id);
  QTest::qWait(150);

  EXPECT_EQ(mService->status(commit->series_id).state, InvitationState::WaitingForSchedule);
  EXPECT_TRUE(mModel.invitationKeys.isEmpty());
  EXPECT_EQ(mStore->writes, 0);

  // Once the server finally acknowledges the schedule the invitation follows,
  // without another call from the UI.
  mModel.neverRespondToPut = false; // the timed-out PUT is retried and acknowledged
  ASSERT_TRUE(waitForInvitation(commit->series_id, InvitationState::Ready, 5000));
  EXPECT_EQ(mModel.invitations, 1);
}

TEST_F(SeriesInvitationTest, PendingLocalEditKeepsNewInvitationWaitingForItsAck) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  mSync->start();
  ASSERT_TRUE(waitFor([&] { return mSync->status(uid).state == ScheduleSyncState::Synced; }));

  // Offline cancel: committed locally, not acknowledged.
  mModel.neverRespondToPut = true;
  ASSERT_TRUE(cancelOccurrence(commit->series_id, 1).has_value());
  mSync->notifyLocalChange(uid);
  ASSERT_TRUE(waitFor([&] { return !mModel.putBodies.isEmpty() && mModel.putBodies.size() >= 2; }));
  EXPECT_TRUE(mSync->status(uid).unconfirmed);

  mService->ensureInvitation(commit->series_id);
  QTest::qWait(150);
  EXPECT_EQ(mService->status(commit->series_id).state, InvitationState::WaitingForSchedule);
  EXPECT_EQ(mModel.invitations, 0);
}

TEST_F(SeriesInvitationTest, SecretGoesToSecureStorageAndNeverToTheDatabase) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  mSync->start();
  QSignalSpy ready(mService.get(), &SeriesInvitationService::invitationReady);

  mService->ensureInvitation(commit->series_id);

  ASSERT_TRUE(waitForInvitation(commit->series_id, InvitationState::Ready));
  EXPECT_EQ(ready.count(), 1);
  ASSERT_TRUE(mStore->secrets.contains(uid));
  EXPECT_EQ(mStore->secrets[uid].passcode, "pass0001");
  EXPECT_TRUE(mStore->secrets[uid].url.startsWith("https://sessio.test/i/"));
  const auto identity = mDb->get_schedule_identity(commit->series_id);
  ASSERT_TRUE(identity.has_value());
  EXPECT_EQ(identity->invitation_generation, 1);
  EXPECT_FALSE(identity->invitation_key.has_value()); // key dropped after storing
  // The secret appears nowhere in the persisted event row or the outbox.
  const auto stored = mDb->get_event_series(commit->series_id);
  EXPECT_FALSE(stored->invitation_state.has_value());
  EXPECT_EQ(stored->meeting_url, "");

  QString url;
  QString passcode;
  bool ok = false;
  mService->loadInvitation(commit->series_id, [&](bool o, const QString &u, const QString &p) {
    ok = o;
    url = u;
    passcode = p;
  });
  EXPECT_TRUE(ok);
  EXPECT_EQ(passcode, "pass0001");
  EXPECT_EQ(url, mStore->secrets[uid].url);
}

TEST_F(SeriesInvitationTest, LostResponseIsReplayedWithTheSameKeyAndCreatesOneInvitation) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  mSync->start();
  mModel.dropNextInvitationReply = true;

  mService->ensureInvitation(commit->series_id);

  ASSERT_TRUE(waitForInvitation(commit->series_id, InvitationState::Ready));
  ASSERT_EQ(mModel.invitationKeys.size(), 2);
  EXPECT_EQ(mModel.invitationKeys[0], mModel.invitationKeys[1]);
  EXPECT_FALSE(mModel.invitationKeys[0].isEmpty());
  EXPECT_EQ(mModel.invitations, 1);
  EXPECT_EQ(mDb->get_schedule_identity(commit->series_id)->invitation_generation, 1);
}

TEST_F(SeriesInvitationTest, UnavailableSecureStorageKeepsTheKeySoRetryReturnsTheSameSecret) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  mSync->start();
  mStore->available = false;

  mService->ensureInvitation(commit->series_id);

  ASSERT_TRUE(waitForInvitation(commit->series_id, InvitationState::Failed));
  EXPECT_EQ(mService->status(commit->series_id).detail, "secret_store_unavailable");
  EXPECT_EQ(mDb->get_schedule_identity(commit->series_id)->invitation_generation, 0);
  EXPECT_TRUE(mDb->get_schedule_identity(commit->series_id)->invitation_key.has_value());

  mStore->available = true;
  mService->retry(commit->series_id);

  ASSERT_TRUE(waitForInvitation(commit->series_id, InvitationState::Ready));
  EXPECT_EQ(mModel.invitations, 1); // replayed, not reissued
  ASSERT_EQ(mModel.invitationKeys.size(), 2);
  EXPECT_EQ(mModel.invitationKeys[0], mModel.invitationKeys[1]);
  EXPECT_EQ(mStore->secrets[uid].passcode, "pass0001");
}

TEST_F(SeriesInvitationTest, EditingTheScheduleNeverReplacesTheExistingInvitation) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  mSync->start();
  mService->ensureInvitation(commit->series_id);
  ASSERT_TRUE(waitForInvitation(commit->series_id, InvitationState::Ready));
  const auto before = mStore->secrets[uid];

  // A schedule edit, a title-only edit (no commit at all) and another ensure.
  ASSERT_TRUE(cancelOccurrence(commit->series_id, 1).has_value());
  mSync->notifyLocalChange(uid);
  ASSERT_TRUE(waitFor([&] { return mSync->status(uid).state == ScheduleSyncState::Synced; }));
  mService->ensureInvitation(commit->series_id);
  QTest::qWait(100);

  EXPECT_EQ(mModel.invitations, 1);
  EXPECT_EQ(mModel.invitationKeys.size(), 1); // no second request of any kind
  EXPECT_EQ(mStore->secrets[uid].passcode, before.passcode);
  EXPECT_EQ(mStore->secrets[uid].url, before.url);
  EXPECT_EQ(mService->status(commit->series_id).state, InvitationState::Ready);
}

TEST_F(SeriesInvitationTest, ServerInvitationWithoutLocalSecretNeedsExplicitReissue) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  mSync->start();
  ASSERT_TRUE(waitFor([&] { return mSync->status(uid).state == ScheduleSyncState::Synced; }));
  mModel.series[uid].invitationGeneration = 1; // created earlier, secret lost

  mService->ensureInvitation(commit->series_id);

  ASSERT_TRUE(waitForInvitation(commit->series_id, InvitationState::NeedsReissue));
  EXPECT_EQ(mStore->writes, 0);

  mService->reissueInvitation(commit->series_id);
  ASSERT_TRUE(waitForInvitation(commit->series_id, InvitationState::Ready));
  EXPECT_EQ(mModel.series[uid].invitationGeneration, 2);
  EXPECT_EQ(mDb->get_schedule_identity(commit->series_id)->invitation_generation, 2);
  EXPECT_EQ(mStore->secrets[uid].generation, 2);
  ASSERT_EQ(mModel.invitationKeys.size(), 2);
  EXPECT_NE(mModel.invitationKeys[0], mModel.invitationKeys[1]); // a new intent, a new key
}

TEST_F(SeriesInvitationTest, CredentialIsReadForEachRequestAndMissingCredentialIsReported) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  mSync->start();
  ASSERT_TRUE(waitFor([&] { return mSync->status(uid).state == ScheduleSyncState::Synced; }));
  const int readsAfterSync = credentialReads;
  credentialAvailable = false;

  mService->ensureInvitation(commit->series_id);

  ASSERT_TRUE(waitForInvitation(commit->series_id, InvitationState::Failed));
  EXPECT_EQ(mService->status(commit->series_id).detail, "credential_unavailable");
  EXPECT_TRUE(mModel.invitationKeys.isEmpty());
  EXPECT_GT(credentialReads, readsAfterSync); // asked at request time, not captured

  credentialAvailable = true; // keychain unlocked later
  mService->retry(commit->series_id);
  ASSERT_TRUE(waitForInvitation(commit->series_id, InvitationState::Ready));
}

TEST_F(SeriesInvitationTest, OfflineBackendWaitsAndRetriesWithoutCreatingDuplicates) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  mSync->start();
  ASSERT_TRUE(waitFor([&] { return mSync->status(uid).state == ScheduleSyncState::Synced; }));
  mModel.invitationStatusOverride = 503;

  mService->ensureInvitation(commit->series_id);
  ASSERT_TRUE(waitForInvitation(commit->series_id, InvitationState::WaitingForNetwork));

  mModel.invitationStatusOverride = 0;
  ASSERT_TRUE(waitForInvitation(commit->series_id, InvitationState::Ready));
  EXPECT_EQ(mModel.invitations, 1);
}

TEST(SeriesJoinTargetTest, RoundTripsTheOriginalStartAndRejectsOrdinaryReferences) {
  const auto uid = QStringLiteral("11111111-2222-4333-8444-555555555555");
  const auto encoded = encodeSeriesJoinTarget(uid, 1791903600000);
  EXPECT_EQ(encoded, "series:11111111-2222-4333-8444-555555555555@2026-10-13T15:00:00Z");
  const auto parsed = parseSeriesJoinTarget(encoded);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->seriesUid, uid);
  EXPECT_EQ(parsed->originalStartMs, 1791903600000);

  EXPECT_FALSE(parseSeriesJoinTarget("ref-new").has_value());
  EXPECT_FALSE(parseSeriesJoinTarget("").has_value());
  EXPECT_FALSE(parseSeriesJoinTarget("series:not-a-uuid@2026-10-13T15:00:00Z").has_value());
  EXPECT_FALSE(
      parseSeriesJoinTarget("series:11111111-2222-4333-8444-555555555555@2026-10-13").has_value());
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST_F(SeriesInvitationTest, ReadyLinkWithoutKeychainSecretCanBeReplacedByAnExplicitReissue) {
  // A backup restored on a new device (or a reset keychain): the database says
  // the series has generation 1, the keychain has nothing.
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  mSync->start();
  mService->ensureInvitation(commit->series_id);
  ASSERT_TRUE(waitForInvitation(commit->series_id, InvitationState::Ready));
  mStore->secrets.clear();

  EXPECT_EQ(mService->status(commit->series_id).state, InvitationState::Ready);
  bool ok = false;
  QString url = "unset";
  mService->loadInvitation(commit->series_id, [&](bool o, const QString &u, const QString &) {
    ok = o;
    url = u;
  });
  EXPECT_TRUE(ok);
  EXPECT_TRUE(url.isEmpty()); // nothing to copy ...

  mService->reissueInvitation(commit->series_id); // ... but a new link can be made
  ASSERT_TRUE(waitFor([&] { return mDb->get_schedule_identity(commit->series_id)->invitation_generation == 2; }));
  ASSERT_TRUE(waitForInvitation(commit->series_id, InvitationState::Ready));
  ASSERT_TRUE(mStore->secrets.contains(uid));
  EXPECT_EQ(mStore->secrets[uid].generation, 2);
}

TEST_F(SeriesInvitationTest, ReissueInterruptedBeforeTheSecretWasStoredResumesAfterRestart) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  const auto uid = QString::fromStdString(commit->series_uid);
  mSync->start();
  mService->ensureInvitation(commit->series_id);
  ASSERT_TRUE(waitForInvitation(commit->series_id, InvitationState::Ready));

  // The server rotates the invitation, but the keychain write fails.
  mStore->available = false;
  mService->reissueInvitation(commit->series_id);
  ASSERT_TRUE(waitForInvitation(commit->series_id, InvitationState::Failed));
  EXPECT_EQ(mModel.invitations, 2);
  auto identity = mDb->get_schedule_identity(commit->series_id);
  ASSERT_TRUE(identity.has_value());
  EXPECT_EQ(identity->invitation_generation, 1); // local state still points at the old link
  ASSERT_TRUE(identity->invitation_key.has_value());

  // Restart: a fresh service, which on its own would claim Ready (generation 1).
  mService = std::make_unique<SeriesInvitationService>(*mDb, *mSync, *mClient, reader(), *mStore);
  mService->setBackoffPolicy([](int) { return 30; });
  mStore->available = true;
  EXPECT_EQ(mService->status(commit->series_id).state, InvitationState::Ready);

  mService->resumeInterruptedReissue(commit->series_id);

  ASSERT_TRUE(waitFor([&] { return mDb->get_schedule_identity(commit->series_id)->invitation_generation == 2; }));
  ASSERT_TRUE(waitForInvitation(commit->series_id, InvitationState::Ready));
  EXPECT_EQ(mModel.invitations, 2); // the same key was replayed, nothing new was created
  ASSERT_EQ(mModel.invitationKeys.size(), 3);
  EXPECT_EQ(mModel.invitationKeys[1], mModel.invitationKeys[2]);
  EXPECT_EQ(mStore->secrets[uid].generation, 2);
  EXPECT_FALSE(mDb->get_schedule_identity(commit->series_id)->invitation_key.has_value());
}

TEST_F(SeriesInvitationTest, ResumeDoesNothingWithoutAPersistedKeyOrWithoutAnInvitation) {
  const auto commit = createSeries();
  ASSERT_TRUE(commit.has_value());
  mSync->start();
  mService->resumeInterruptedReissue(commit->series_id); // generation 0: not a reissue
  QTest::qWait(100);
  EXPECT_TRUE(mModel.invitationKeys.isEmpty());
  mService->ensureInvitation(commit->series_id);
  ASSERT_TRUE(waitForInvitation(commit->series_id, InvitationState::Ready));
  mService->resumeInterruptedReissue(commit->series_id); // no key left
  QTest::qWait(100);
  EXPECT_EQ(mModel.invitationKeys.size(), 1);
}
