#include "crypto/hashing.h"
#include "db/accounts_repository.h"
#include "db/invitations_repository.h"
#include "db/meetings_repository.h"
#include "db/migrations.h"
#include "db/sqlite_connection.h"

#include <gtest/gtest.h>
#include <sodium.h>

class InvitationsRepositoryTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(sodium_init() >= 0, true);
    conn = std::make_unique<pcm::tokenbackend::SqliteConnection>(":memory:");
    pcm::tokenbackend::runMigrations(*conn);
    // Meetings and invitations both hold a NOT NULL foreign key to
    // accounts(id), and foreign_keys enforcement is ON (see
    // SqliteConnection). Seed an account so the literal accountId=1 used
    // throughout these tests is a valid reference.
    pcm::tokenbackend::AccountsRepository accounts(*conn);
    accounts.seedAccount();
    pcm::tokenbackend::MeetingsRepository meetings(*conn);
    meeting = meetings.create(1, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");
  }
  std::unique_ptr<pcm::tokenbackend::SqliteConnection> conn;
  pcm::tokenbackend::Meeting meeting{};
};

TEST_F(InvitationsRepositoryTest, CreateThenFindByCodeSucceeds) {
  pcm::tokenbackend::InvitationsRepository repo(*conn);
  auto created = repo.create(meeting.id, 1);

  auto found = repo.findByCode(created.invitationCode);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->meetingId, meeting.id);
  EXPECT_EQ(found->passcodeAttempts, 0);
  EXPECT_TRUE(pcm::tokenbackend::passcodeMatches(created.passcode, found->passcodeHash));
}

TEST_F(InvitationsRepositoryTest, WrongCodeReturnsNullopt) {
  pcm::tokenbackend::InvitationsRepository repo(*conn);
  repo.create(meeting.id, 1);

  EXPECT_FALSE(repo.findByCode("wrong-code").has_value());
}

TEST_F(InvitationsRepositoryTest, FailedAttemptsAccumulateAndAutoInvalidateAtFive) {
  pcm::tokenbackend::InvitationsRepository repo(*conn);
  auto created = repo.create(meeting.id, 1);

  int attempts = 0;
  for (int i = 0; i < 5; ++i) {
    attempts = repo.recordFailedPasscodeAttempt(created.invitation.id);
  }
  EXPECT_EQ(attempts, 5);

  auto found = repo.findByCode(created.invitationCode);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->status, "invalidated");
}

TEST_F(InvitationsRepositoryTest, ExplicitInvalidateChangesStatus) {
  pcm::tokenbackend::InvitationsRepository repo(*conn);
  auto created = repo.create(meeting.id, 1);

  repo.invalidate(created.invitation.id);

  auto found = repo.findByCode(created.invitationCode);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->status, "invalidated");
}
