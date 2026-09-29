#include "db/accounts_repository.h"
#include "db/migrations.h"
#include "db/sqlite_connection.h"

#include <gtest/gtest.h>
#include <sodium.h>

class AccountsRepositoryTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(sodium_init() >= 0, true);
    conn = std::make_unique<pcm::tokenbackend::SqliteConnection>(":memory:");
    pcm::tokenbackend::runMigrations(*conn);
  }
  std::unique_ptr<pcm::tokenbackend::SqliteConnection> conn;
};

TEST_F(AccountsRepositoryTest, SeedThenFindByCredentialSucceeds) {
  pcm::tokenbackend::AccountsRepository repo(*conn);
  auto credential = repo.seedAccount();

  auto found = repo.findByCredential(credential);
  ASSERT_TRUE(found.has_value());
}

TEST_F(AccountsRepositoryTest, WrongCredentialReturnsNullopt) {
  pcm::tokenbackend::AccountsRepository repo(*conn);
  repo.seedAccount();

  EXPECT_FALSE(repo.findByCredential("not-the-credential").has_value());
}

TEST_F(AccountsRepositoryTest, ReseedingReplacesThePreviousCredential) {
  pcm::tokenbackend::AccountsRepository repo(*conn);
  auto first = repo.seedAccount();
  auto second = repo.seedAccount();

  EXPECT_FALSE(repo.findByCredential(first).has_value());
  EXPECT_TRUE(repo.findByCredential(second).has_value());
}

TEST_F(AccountsRepositoryTest, CreateAccountAddsWithoutRemovingExisting) {
  pcm::tokenbackend::AccountsRepository repo(*conn);
  auto first = repo.seedAccount();
  auto second = repo.createAccount();

  EXPECT_TRUE(repo.findByCredential(first).has_value());
  EXPECT_TRUE(repo.findByCredential(second).has_value());
}

TEST_F(AccountsRepositoryTest, ListAccountsReturnsAllInIdOrder) {
  pcm::tokenbackend::AccountsRepository repo(*conn);
  repo.seedAccount();
  repo.createAccount();
  repo.createAccount();

  auto list = repo.listAccounts();
  ASSERT_EQ(list.size(), 3u);
  EXPECT_LT(list[0].id, list[1].id);
  EXPECT_LT(list[1].id, list[2].id);
  for (const auto &summary : list) {
    EXPECT_FALSE(summary.createdAt.empty());
  }
}

TEST_F(AccountsRepositoryTest, ListAccountsEmptyWhenNoneExist) {
  pcm::tokenbackend::AccountsRepository repo(*conn);
  EXPECT_TRUE(repo.listAccounts().empty());
}

TEST_F(AccountsRepositoryTest, RevokeAccountRemovesAnUnusedAccount) {
  pcm::tokenbackend::AccountsRepository repo(*conn);
  auto credential = repo.seedAccount();
  auto id = repo.listAccounts().front().id;

  EXPECT_EQ(repo.revokeAccount(id), pcm::tokenbackend::RevokeResult::Removed);
  EXPECT_FALSE(repo.findByCredential(credential).has_value());
}

TEST_F(AccountsRepositoryTest, RevokeAccountReturnsNotFoundForUnknownId) {
  pcm::tokenbackend::AccountsRepository repo(*conn);
  EXPECT_EQ(repo.revokeAccount(999999), pcm::tokenbackend::RevokeResult::NotFound);
}

TEST_F(AccountsRepositoryTest, RevokeAccountReturnsInUseWhenMeetingsReferenceIt) {
  pcm::tokenbackend::AccountsRepository repo(*conn);
  auto credential = repo.seedAccount();
  auto id = repo.listAccounts().front().id;

  conn->exec("INSERT INTO meetings (meeting_ref, account_id, room_name, scheduled_start, "
             "scheduled_end, created_at) VALUES ('mtg_test1', " +
             std::to_string(id) +
             ", 'rm_test1', '2026-01-01T10:00:00Z', '2026-01-01T10:30:00Z', "
             "'2026-01-01T09:00:00Z');");

  EXPECT_EQ(repo.revokeAccount(id), pcm::tokenbackend::RevokeResult::InUse);
  EXPECT_TRUE(repo.findByCredential(credential).has_value());
}
