#include "db/schedule_repository.h"
#include "db/migrations.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <latch>
#include <thread>
#include <unistd.h>
#include "db/meetings_repository.h"
#include "db/invitations_repository.h"

using namespace pcm::tokenbackend;
namespace {
constexpr const char *uid = "45f9c587-94c8-4d45-9f21-422a8df32590";
pcm::schedule::Snapshot sample() {
  pcm::schedule::Snapshot s;
  s.revision = 1; s.timezone = "Europe/Moscow";
  s.dtstartLocal = "2026-10-06T18:00:00"; s.durationSeconds = 3600;
  s.rrule = "FREQ=WEEKLY;INTERVAL=1;BYDAY=TU";
  s.active = true; s.joinEnabled = true;
  return s;
}
class ScheduleRepositoryTest : public ::testing::Test {
protected:
  SqliteConnection conn{":memory:"};
  ScheduleRepository repo{conn};
  AccountId account;
  void SetUp() override {
    runMigrations(conn);
    AccountsRepository accounts(conn);
    account = *accounts.findByCredential(accounts.createAccount());
  }
};
TEST_F(ScheduleRepositoryTest, FirstSnapshotIsReadableWithServerContentHash) {
  auto saved = repo.put(account, uid, sample());
  ASSERT_EQ(saved.status, ScheduleWriteStatus::Applied);
  auto loaded = repo.get(account, uid);
  ASSERT_TRUE(loaded);
  EXPECT_EQ(loaded->snapshot.revision, 1);
  EXPECT_EQ(loaded->snapshot.rrule, sample().rrule);
  EXPECT_EQ(loaded->contentHash.size(), 64);
  EXPECT_EQ(loaded->contentHash, saved.value->contentHash);
}
TEST_F(ScheduleRepositoryTest, RetryIsIdempotentAndOnlyCurrentBaseCanReplaceCollections) {
  auto s = sample();
  s.overrides = {{1791903600000, 1791990000000, 1791993600000, true},
                 {1792508400000, 1792594800000, 1792598400000, false}};
  s.exceptions = {1792508400000, 1791903600000};
  auto first = repo.put(account, uid, s);
  ASSERT_EQ(first.status, ScheduleWriteStatus::Applied);
  std::reverse(s.overrides.begin(), s.overrides.end());
  std::reverse(s.exceptions.begin(), s.exceptions.end());
  auto retry = repo.put(account, uid, s);
  ASSERT_EQ(retry.status, ScheduleWriteStatus::Applied);
  EXPECT_EQ(retry.value->contentHash, first.value->contentHash);
  s.active = false;
  EXPECT_EQ(repo.put(account, uid, s).status, ScheduleWriteStatus::Conflict);
  s.revision = 2; s.baseRevision = 1;
  s.overrides.clear(); s.exceptions.clear();
  ASSERT_EQ(repo.put(account, uid, s).status, ScheduleWriteStatus::Applied);
  EXPECT_EQ(repo.put(account, uid, sample()).status, ScheduleWriteStatus::Conflict);
  auto final = repo.get(account, uid);
  ASSERT_TRUE(final);
  EXPECT_FALSE(final->snapshot.active);
  EXPECT_TRUE(final->snapshot.overrides.empty());
  EXPECT_TRUE(final->snapshot.exceptions.empty());
  EXPECT_NE(final->contentHash, first.value->contentHash);
}
TEST_F(ScheduleRepositoryTest, OtherAccountCannotReadOrReplaceOrClaimExistingUid) {
  ASSERT_EQ(repo.put(account, uid, sample()).status, ScheduleWriteStatus::Applied);
  AccountsRepository accounts(conn);
  auto other = *accounts.findByCredential(accounts.createAccount());
  EXPECT_FALSE(repo.get(other, uid));
  EXPECT_EQ(repo.put(other, uid, sample()).status, ScheduleWriteStatus::NotFound);
}
TEST_F(ScheduleRepositoryTest, InvalidReplacementLeavesConfirmedSnapshotIntact) {
  ASSERT_EQ(repo.put(account, uid, sample()).status, ScheduleWriteStatus::Applied);
  auto invalid = sample(); invalid.revision = 2; invalid.baseRevision = 1;
  invalid.overrides = {{1791903600000, 2, 1, true}};
  EXPECT_EQ(repo.put(account, uid, invalid).status, ScheduleWriteStatus::Invalid);
  EXPECT_EQ(repo.get(account, uid)->snapshot.revision, 1);
}
TEST_F(ScheduleRepositoryTest, UuidCaseIsCanonicalAndInvalidIdentifiersCannotCreateResources) {
  EXPECT_EQ(repo.put(account, "invalid", sample()).status, ScheduleWriteStatus::Invalid);
  const char *upper = "45F9C587-94C8-4D45-9F21-422A8DF32590";
  auto created = repo.put(account, upper, sample());
  ASSERT_EQ(created.status, ScheduleWriteStatus::Applied);
  EXPECT_EQ(created.value->seriesUid, uid);
  ASSERT_TRUE(repo.get(account, uid));
  EXPECT_EQ(repo.get(account, upper)->seriesUid, uid);
  auto s = sample(); s.baseRevision = 1; s.revision = 2;
  auto next = repo.put(account, uid, s); ASSERT_TRUE(next.value);
  EXPECT_EQ(next.value->contentHash, created.value->contentHash);
}
TEST_F(ScheduleRepositoryTest, SimultaneousDifferentCasWritesHaveExactlyOneWinner) {
  ASSERT_EQ(repo.put(account, uid, sample()).status, ScheduleWriteStatus::Applied);
  std::latch start(1); std::atomic<int> applied{0}, conflicts{0};
  auto write = [&](bool active) {
    auto s = sample(); s.revision = 2; s.baseRevision = 1; s.active = active;
    start.wait();
    try {
      auto r = repo.put(account, uid, s);
      if (r.status == ScheduleWriteStatus::Applied) ++applied;
      if (r.status == ScheduleWriteStatus::Conflict) ++conflicts;
    } catch (...) { }
  };
  std::thread a(write, true), b(write, false); start.count_down(); a.join(); b.join();
  EXPECT_EQ(applied, 1); EXPECT_EQ(conflicts, 1);
  EXPECT_EQ(repo.get(account, uid)->snapshot.revision, 2);
}
TEST_F(ScheduleRepositoryTest, SqlFailureRollsBackParentAndBothCollections) {
  auto s = sample(); s.exceptions = {1791903600000};
  auto initial = repo.put(account, uid, s);
  ASSERT_EQ(initial.status, ScheduleWriteStatus::Applied);
  { auto guard = conn.lock(); conn.exec("CREATE TRIGGER fail_override BEFORE INSERT ON schedule_overrides BEGIN SELECT RAISE(ABORT,'injected failure'); END;"); }
  s.revision = 2; s.baseRevision = 1; s.exceptions.clear(); s.active = false;
  s.overrides = {{1791903600000, 1791990000000, 1791993600000, true}};
  EXPECT_THROW(repo.put(account, uid, s), std::runtime_error);
  auto restored = repo.get(account, uid);
  ASSERT_TRUE(restored);
  EXPECT_EQ(restored->snapshot.revision, 1);
  EXPECT_TRUE(restored->snapshot.active);
  EXPECT_EQ(restored->snapshot.exceptions, initial.value->snapshot.exceptions);
  EXPECT_TRUE(restored->snapshot.overrides.empty());
  EXPECT_EQ(restored->contentHash, initial.value->contentHash);
}
TEST(ScheduleDatabase, ReopenAndRepeatedAdditiveMigrationPreserveLegacyAndSchedules) {
  char path[] = "/tmp/pcm-schedule-XXXXXX";
  int fd = mkstemp(path); ASSERT_GE(fd, 0); close(fd);
  AccountId account; std::string meetingRef, invitationCode, hash;
  {
    SqliteConnection conn(path);
    // Start with the previous released schema and populated legacy rows;
    // schedule tables must be added by this migration rather than preexist.
    conn.exec(R"sql(
      CREATE TABLE accounts(id INTEGER PRIMARY KEY, credential_hash TEXT NOT NULL UNIQUE, created_at TEXT NOT NULL);
      CREATE TABLE meetings(id INTEGER PRIMARY KEY, meeting_ref TEXT NOT NULL UNIQUE, account_id INTEGER NOT NULL REFERENCES accounts(id), room_name TEXT NOT NULL UNIQUE, scheduled_start TEXT NOT NULL, scheduled_end TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'active', created_at TEXT NOT NULL);
      CREATE TABLE invitations(id INTEGER PRIMARY KEY, meeting_id INTEGER NOT NULL REFERENCES meetings(id), account_id INTEGER NOT NULL REFERENCES accounts(id), invitation_code_hash TEXT NOT NULL UNIQUE, passcode_hash TEXT NOT NULL, passcode_attempts INTEGER NOT NULL DEFAULT 0, status TEXT NOT NULL DEFAULT 'active', created_at TEXT NOT NULL);
    )sql");
    AccountsRepository accounts(conn); account = *accounts.findByCredential(accounts.createAccount());
    MeetingsRepository meetings(conn);
    auto meeting = meetings.create(account, "2026-10-06T15:00:00Z", "2026-10-06T16:00:00Z");
    meetingRef = meeting.meetingRef;
    InvitationsRepository invitations(conn);
    invitationCode = invitations.create(meeting.id, account).invitationCode;
    runMigrations(conn);
    ASSERT_TRUE(meetings.findByRef(meetingRef));
    ASSERT_TRUE(invitations.findByCode(invitationCode));
    ScheduleRepository repo(conn);
    auto s = sample(); s.exceptions = {1791903600000};
    auto saved = repo.put(account, uid, s); ASSERT_TRUE(saved.value); hash = saved.value->contentHash;
  }
  {
    SqliteConnection conn(path); runMigrations(conn); runMigrations(conn);
    MeetingsRepository meetings(conn); ASSERT_TRUE(meetings.findByRef(meetingRef));
    InvitationsRepository invitations(conn); ASSERT_TRUE(invitations.findByCode(invitationCode));
    ScheduleRepository repo(conn); auto loaded = repo.get(account, uid); ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded->contentHash, hash); EXPECT_EQ(loaded->snapshot.exceptions.size(), 1);
  }
  std::filesystem::remove(path);
}
}
