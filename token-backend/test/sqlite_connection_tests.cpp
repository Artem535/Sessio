#include "db/sqlite_connection.h"
#include "db/migrations.h"

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <stdexcept>
#include <string>
#include <thread>

namespace {

int countRows(pcm::tokenbackend::SqliteConnection &conn, const std::string &sql) {
  sqlite3_stmt *stmt = nullptr;
  EXPECT_EQ(sqlite3_prepare_v2(conn.raw(), sql.c_str(), -1, &stmt, nullptr), SQLITE_OK);
  int value = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    value = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return value;
}

} // namespace

TEST(SqliteConnectionTest, ExecRunsStatement) {
  pcm::tokenbackend::SqliteConnection conn(":memory:");
  conn.exec("CREATE TABLE t (id INTEGER PRIMARY KEY)");
  conn.exec("INSERT INTO t (id) VALUES (1)");

  sqlite3_stmt *stmt = nullptr;
  sqlite3_prepare_v2(conn.raw(), "SELECT COUNT(*) FROM t", -1, &stmt, nullptr);
  sqlite3_step(stmt);
  EXPECT_EQ(sqlite3_column_int(stmt, 0), 1);
  sqlite3_finalize(stmt);
}

// The connection is shared across oat++'s worker threads, so serialized mode
// is requested explicitly (SQLITE_OPEN_FULLMUTEX) rather than inherited from
// however libsqlite3 was compiled.
TEST(SqliteConnectionTest, LibraryIsThreadsafe) {
  EXPECT_NE(sqlite3_threadsafe(), 0)
      << "libsqlite3 was compiled with SQLITE_THREADSAFE=0; SQLITE_OPEN_FULLMUTEX "
         "cannot rescue that and the shared connection is unsafe";
}

// Each of these takes the connection lock before opening the transaction, the
// way every repository method does. That is a precondition SqliteTransaction
// now enforces — before it did, these three tests were quietly violating it.
TEST(SqliteTransactionTest, CommitPersistsEveryStatementInTheSequence) {
  pcm::tokenbackend::SqliteConnection conn(":memory:");
  conn.exec("CREATE TABLE t (id INTEGER PRIMARY KEY)");

  {
    auto guard = conn.lock();
    pcm::tokenbackend::SqliteTransaction tx(conn);
    conn.exec("INSERT INTO t (id) VALUES (1)");
    conn.exec("INSERT INTO t (id) VALUES (2)");
    tx.commit();
  }

  EXPECT_EQ(countRows(conn, "SELECT COUNT(*) FROM t"), 2);
}

TEST(SqliteTransactionTest, RollsBackWhenScopeExitsWithoutCommit) {
  pcm::tokenbackend::SqliteConnection conn(":memory:");
  conn.exec("CREATE TABLE t (id INTEGER PRIMARY KEY)");
  conn.exec("INSERT INTO t (id) VALUES (1)");

  {
    auto guard = conn.lock();
    pcm::tokenbackend::SqliteTransaction tx(conn);
    conn.exec("DELETE FROM t");
    conn.exec("INSERT INTO t (id) VALUES (2)");
    // No commit() — the destructor must undo both statements. It is destroyed
    // before `guard`, so the ROLLBACK still runs under the lock.
  }

  EXPECT_EQ(countRows(conn, "SELECT COUNT(*) FROM t"), 1);
  EXPECT_EQ(countRows(conn, "SELECT id FROM t"), 1) << "the original row must survive";
}

TEST(SqliteTransactionTest, RollsBackWhenAStatementThrowsMidSequence) {
  pcm::tokenbackend::SqliteConnection conn(":memory:");
  conn.exec("CREATE TABLE t (id INTEGER PRIMARY KEY)");
  conn.exec("INSERT INTO t (id) VALUES (1)");

  EXPECT_THROW(
      {
        auto guard = conn.lock();
        pcm::tokenbackend::SqliteTransaction tx(conn);
        conn.exec("DELETE FROM t");
        conn.exec("INSERT INTO nonexistent_table (id) VALUES (2)"); // throws
        tx.commit();
      },
      std::runtime_error);

  EXPECT_EQ(countRows(conn, "SELECT COUNT(*) FROM t"), 1)
      << "the DELETE must not survive a failure later in the sequence";
}

// --- the two enforced preconditions -----------------------------------------
//
// Both of these used to be comment-only. The recursive lock is what makes their
// violation silent — it will not deadlock on a re-entrant transaction the way a
// plain mutex would, so the mistake would compile, run, and only surface in
// production under concurrency as "cannot start a transaction within a
// transaction".

TEST(SqliteTransactionTest, RefusesToOpenWithoutTheConnectionLockHeld) {
  pcm::tokenbackend::SqliteConnection conn(":memory:");
  conn.exec("CREATE TABLE t (id INTEGER PRIMARY KEY)");

  EXPECT_FALSE(conn.heldByCurrentThread());
  EXPECT_THROW(pcm::tokenbackend::SqliteTransaction tx(conn), std::logic_error);

  // The failed constructor must leave nothing behind: no open transaction, and
  // a subsequent well-formed transaction still works.
  {
    auto guard = conn.lock();
    pcm::tokenbackend::SqliteTransaction tx(conn);
    conn.exec("INSERT INTO t (id) VALUES (1)");
    tx.commit();
  }
  EXPECT_EQ(countRows(conn, "SELECT COUNT(*) FROM t"), 1);
}

TEST(SqliteTransactionTest, RefusesToNestASecondTransactionOnTheSameConnection) {
  pcm::tokenbackend::SqliteConnection conn(":memory:");
  conn.exec("CREATE TABLE t (id INTEGER PRIMARY KEY)");

  auto guard = conn.lock();
  pcm::tokenbackend::SqliteTransaction outer(conn);
  conn.exec("INSERT INTO t (id) VALUES (1)");

  // Same thread, lock legitimately held (recursively, as a nested repository
  // call would hold it) — so only the open-transaction check can catch this.
  EXPECT_THROW(
      {
        auto innerGuard = conn.lock();
        pcm::tokenbackend::SqliteTransaction inner(conn);
      },
      std::logic_error);

  // The refusal must not have disturbed the transaction already in progress.
  outer.commit();
  EXPECT_EQ(countRows(conn, "SELECT COUNT(*) FROM t"), 1);
}

TEST(SqliteTransactionTest, LockOwnershipIsReleasedWhenTheGuardGoesOutOfScope) {
  pcm::tokenbackend::SqliteConnection conn(":memory:");

  EXPECT_FALSE(conn.heldByCurrentThread());
  {
    auto guard = conn.lock();
    EXPECT_TRUE(conn.heldByCurrentThread());
    {
      // Re-entrant acquisition, as a nested repository call performs.
      auto nested = conn.lock();
      EXPECT_TRUE(conn.heldByCurrentThread());
    }
    EXPECT_TRUE(conn.heldByCurrentThread())
        << "releasing the inner guard must not drop the outer one's ownership";
  }
  EXPECT_FALSE(conn.heldByCurrentThread());
}

TEST(SqliteTransactionTest, AnotherThreadIsNotSeenAsHoldingTheLock) {
  pcm::tokenbackend::SqliteConnection conn(":memory:");

  auto guard = conn.lock();
  ASSERT_TRUE(conn.heldByCurrentThread());

  // The ownership check must be per-thread, not a bare "is it locked" flag:
  // otherwise a thread that never took the lock could open a transaction
  // while this one holds it, which is precisely the bug being guarded.
  bool otherThreadSawItHeld = true;
  bool otherThreadTransactionThrew = false;
  std::thread other([&] {
    otherThreadSawItHeld = conn.heldByCurrentThread();
    try {
      pcm::tokenbackend::SqliteTransaction tx(conn);
    } catch (const std::logic_error &) {
      otherThreadTransactionThrew = true;
    }
  });
  other.join();

  EXPECT_FALSE(otherThreadSawItHeld);
  EXPECT_TRUE(otherThreadTransactionThrew);
}

TEST(MigrationsTest, CreatesAllThreeTables) {
  pcm::tokenbackend::SqliteConnection conn(":memory:");
  pcm::tokenbackend::runMigrations(conn);

  for (const std::string &table : {"accounts", "meetings", "invitations"}) {
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(conn.raw(),
                        "SELECT name FROM sqlite_master WHERE type='table' AND name=?", -1,
                        &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, table.c_str(), -1, SQLITE_TRANSIENT);
    EXPECT_EQ(sqlite3_step(stmt), SQLITE_ROW) << "missing table " << table;
    sqlite3_finalize(stmt);
  }
}
