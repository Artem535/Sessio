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
