#include "db/accounts_repository.h"
#include "db/meetings_repository.h"
#include "db/migrations.h"
#include "db/sqlite_connection.h"

#include <gtest/gtest.h>
#include <sodium.h>

class MeetingsRepositoryTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(sodium_init() >= 0, true);
    conn = std::make_unique<pcm::tokenbackend::SqliteConnection>(":memory:");
    pcm::tokenbackend::runMigrations(*conn);
    // Meetings hold a NOT NULL foreign key to accounts(id), and foreign_keys
    // enforcement is ON (see SqliteConnection). Seed an account so the
    // literal accountId=1 used throughout these tests is a valid reference.
    pcm::tokenbackend::AccountsRepository accounts(*conn);
    accounts.seedAccount();
  }
  std::unique_ptr<pcm::tokenbackend::SqliteConnection> conn;
};

TEST_F(MeetingsRepositoryTest, CreateProducesUniqueRefAndRoomName) {
  pcm::tokenbackend::MeetingsRepository repo(*conn);
  auto a = repo.create(1, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");
  auto b = repo.create(1, "2026-10-02T10:00:00Z", "2026-10-02T10:50:00Z");

  EXPECT_NE(a.meetingRef, b.meetingRef);
  EXPECT_NE(a.roomName, b.roomName);
  EXPECT_EQ(a.status, "active");
}

TEST_F(MeetingsRepositoryTest, FindByRefReturnsTheSameRoomNameEveryTime) {
  pcm::tokenbackend::MeetingsRepository repo(*conn);
  auto created = repo.create(1, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");

  auto first = repo.findByRef(created.meetingRef);
  auto second = repo.findByRef(created.meetingRef);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->roomName, second->roomName);
  EXPECT_EQ(first->roomName, created.roomName);
}

TEST_F(MeetingsRepositoryTest, InvalidateChangesStatus) {
  pcm::tokenbackend::MeetingsRepository repo(*conn);
  auto created = repo.create(1, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");

  repo.invalidate(created.id);

  auto found = repo.findByRef(created.meetingRef);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->status, "invalidated");
}

TEST_F(MeetingsRepositoryTest, FindByUnknownRefReturnsNullopt) {
  pcm::tokenbackend::MeetingsRepository repo(*conn);
  EXPECT_FALSE(repo.findByRef("mtg_does_not_exist").has_value());
}

TEST_F(MeetingsRepositoryTest, FindByIdReturnsSameRowAsFindByRef) {
  pcm::tokenbackend::MeetingsRepository repo(*conn);
  auto created = repo.create(1, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");

  auto byId = repo.findById(created.id);
  ASSERT_TRUE(byId.has_value());
  EXPECT_EQ(byId->meetingRef, created.meetingRef);
  EXPECT_EQ(byId->roomName, created.roomName);
}
