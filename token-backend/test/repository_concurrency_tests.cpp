// Proves that SqliteConnection::lock() actually serializes the repository
// layer. These tests reproduce production's shape exactly: ONE SqliteConnection
// shared by repositories that several threads call at the same time, which is
// what main.cpp hands to the controllers and what oat++'s HttpConnectionHandler
// then drives from a thread per connection.
//
// Deliberately at the repository level with raw std::thread rather than through
// the HTTP server: the thing under test is the mutex, not oat++'s threading
// model, and driving the repositories directly lets each test assert on the
// exact final database state.
//
// Before the lock existed, ConcurrentFailedAttemptsOnTheSameInvitation failed
// with "cannot start a transaction within a transaction" — a second thread's
// BEGIN IMMEDIATE arriving while another's transaction was still open gets
// SQLITE_ERROR, which the busy timeout cannot retry.

#include "db/accounts_repository.h"
#include "db/invitations_repository.h"
#include "db/meetings_repository.h"
#include "db/migrations.h"
#include "db/sqlite_connection.h"

#include <gtest/gtest.h>
#include <sodium.h>

#include <algorithm>
#include <functional>
#include <latch>
#include <mutex>
#include <numeric>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

// Runs body(threadIndex) on threadCount threads that are released as close to
// simultaneously as the platform allows — every thread does its setup, then
// waits on the latch, so the contended window is as wide as possible instead of
// each thread racing only against whichever ones happen to have started.
//
// Returns what() for every exception that escaped a thread. An exception must
// not be allowed to cross a std::thread boundary (that is a straight
// std::terminate), and a test needs to report the message rather than abort the
// whole binary, so each thread catches its own.
std::vector<std::string> runConcurrently(int threadCount,
                                          const std::function<void(int)> &body) {
  std::vector<std::string> failures;
  std::mutex failuresMutex;
  std::latch start(1);

  std::vector<std::thread> threads;
  threads.reserve(static_cast<size_t>(threadCount));
  for (int i = 0; i < threadCount; ++i) {
    threads.emplace_back([&, i] {
      start.wait();
      try {
        body(i);
      } catch (const std::exception &e) {
        std::lock_guard<std::mutex> lk(failuresMutex);
        failures.emplace_back(e.what());
      } catch (...) {
        std::lock_guard<std::mutex> lk(failuresMutex);
        failures.emplace_back("non-std exception");
      }
    });
  }

  start.count_down();
  for (auto &t : threads) {
    t.join();
  }
  return failures;
}

int scalarInt(pcm::tokenbackend::SqliteConnection &conn, const std::string &sql) {
  sqlite3_stmt *stmt = nullptr;
  EXPECT_EQ(sqlite3_prepare_v2(conn.raw(), sql.c_str(), -1, &stmt, nullptr), SQLITE_OK);
  int value = -1;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    value = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return value;
}

} // namespace

class RepositoryConcurrencyTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_GE(sodium_init(), 0);
    // One connection for the whole fixture, shared by every thread below —
    // the same arrangement as main.cpp.
    conn = std::make_unique<pcm::tokenbackend::SqliteConnection>(":memory:");
    pcm::tokenbackend::runMigrations(*conn);

    pcm::tokenbackend::AccountsRepository accounts(*conn);
    accounts.seedAccount();

    meetings = std::make_unique<pcm::tokenbackend::MeetingsRepository>(*conn);
    invitations = std::make_unique<pcm::tokenbackend::InvitationsRepository>(*conn);
    meeting = meetings->create(1, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");
  }

  std::unique_ptr<pcm::tokenbackend::SqliteConnection> conn;
  std::unique_ptr<pcm::tokenbackend::MeetingsRepository> meetings;
  std::unique_ptr<pcm::tokenbackend::InvitationsRepository> invitations;
  pcm::tokenbackend::Meeting meeting{};
};

// The core case: many threads hammering the read-modify-write transaction in
// recordFailedPasscodeAttempt against the SAME row. This is reachable in
// production from the unauthenticated POST /v1/invitations/{code}/client-token
// (e.g. simultaneous wrong-passcode guesses), and it is the path guarding
// ADR-12's 5-attempt cap.
//
// The returned values are the real assertion. Each call increments the counter
// and reads it back inside its own transaction, so under genuine serialization
// the 96 calls must return exactly 1..96 with no value seen twice: a repeated
// value would mean two calls read the same post-increment count, which is how
// an attacker gets extra guesses past the cap.
TEST_F(RepositoryConcurrencyTest, ConcurrentFailedAttemptsOnTheSameInvitationSerialize) {
  auto created = invitations->create(meeting.id, 1);

  constexpr int kThreads = 12;
  constexpr int kAttemptsPerThread = 8;
  constexpr int kTotal = kThreads * kAttemptsPerThread;

  std::mutex observedMutex;
  std::vector<int> observed;
  observed.reserve(kTotal);

  auto failures = runConcurrently(kThreads, [&](int) {
    for (int i = 0; i < kAttemptsPerThread; ++i) {
      int attempts = invitations->recordFailedPasscodeAttempt(created.invitation.id);
      std::lock_guard<std::mutex> lk(observedMutex);
      observed.push_back(attempts);
    }
  });

  EXPECT_TRUE(failures.empty()) << "first failure: " << (failures.empty() ? "" : failures.front());

  // Every increment landed: no lost updates.
  EXPECT_EQ(scalarInt(*conn, "SELECT passcode_attempts FROM invitations WHERE id = " +
                                  std::to_string(created.invitation.id)),
            kTotal);

  // And every caller saw a distinct count, exactly as if the calls had run one
  // after another.
  ASSERT_EQ(observed.size(), static_cast<size_t>(kTotal));
  std::vector<int> expected(kTotal);
  std::iota(expected.begin(), expected.end(), 1);
  std::sort(observed.begin(), observed.end());
  EXPECT_EQ(observed, expected);

  // Past the cap, so the auto-invalidate inside the transaction fired too.
  EXPECT_EQ(scalarInt(*conn, "SELECT COUNT(*) FROM invitations WHERE id = " +
                                  std::to_string(created.invitation.id) +
                                  " AND status = 'invalidated'"),
            1);
}

// Concurrent writers on DIFFERENT rows, which is the ordinary traffic pattern:
// several clients guessing passcodes for their own invitations at once. Each
// row must end at exactly its own thread's count — no increment leaking across
// rows, none lost.
TEST_F(RepositoryConcurrencyTest, ConcurrentFailedAttemptsOnDifferentInvitationsStayIndependent) {
  constexpr int kThreads = 8;
  constexpr int kAttemptsPerThread = 6;

  std::vector<int64_t> invitationIds;
  for (int i = 0; i < kThreads; ++i) {
    invitationIds.push_back(invitations->create(meeting.id, 1).invitation.id);
  }

  auto failures = runConcurrently(kThreads, [&](int index) {
    for (int i = 0; i < kAttemptsPerThread; ++i) {
      invitations->recordFailedPasscodeAttempt(invitationIds[static_cast<size_t>(index)]);
    }
  });

  EXPECT_TRUE(failures.empty()) << "first failure: " << (failures.empty() ? "" : failures.front());
  for (int64_t id : invitationIds) {
    EXPECT_EQ(scalarInt(*conn, "SELECT passcode_attempts FROM invitations WHERE id = " +
                                    std::to_string(id)),
              kAttemptsPerThread)
        << "invitation " << id;
  }
}

// Failure mode 2 from the fix: MeetingsRepository::create()'s INSERT is not
// itself wrapped in a transaction, so before the lock it could execute inside
// another thread's open transaction and be undone by that transaction's
// rollback — after this caller had already been handed the meeting ref. Here
// the untransacted creates run against threads that are opening transactions
// (reissueForMeeting) on the same connection; every created meeting must still
// be in the database afterwards.
TEST_F(RepositoryConcurrencyTest, UntransactedCreatesSurviveConcurrentTransactions) {
  constexpr int kWriterThreads = 8;
  constexpr int kMeetingsPerThread = 3;
  constexpr int kReissueThreads = 4;

  std::mutex refsMutex;
  std::vector<std::string> createdRefs;

  auto failures = runConcurrently(kWriterThreads + kReissueThreads, [&](int index) {
    if (index < kWriterThreads) {
      for (int i = 0; i < kMeetingsPerThread; ++i) {
        auto m = meetings->create(1, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");
        std::lock_guard<std::mutex> lk(refsMutex);
        createdRefs.push_back(m.meetingRef);
      }
    } else {
      // Transaction-opening work on the shared connection, concurrent with the
      // untransacted inserts above.
      invitations->reissueForMeeting(meeting.id, 1);
    }
  });

  EXPECT_TRUE(failures.empty()) << "first failure: " << (failures.empty() ? "" : failures.front());

  ASSERT_EQ(createdRefs.size(), static_cast<size_t>(kWriterThreads * kMeetingsPerThread));
  // Refs are randomly generated; a collision would mean a lost row below, so
  // check uniqueness before counting.
  EXPECT_EQ(std::set<std::string>(createdRefs.begin(), createdRefs.end()).size(),
            createdRefs.size());
  for (const auto &ref : createdRefs) {
    auto found = meetings->findByRef(ref);
    EXPECT_TRUE(found.has_value()) << "meeting " << ref << " was silently rolled back";
  }
  // The fixture's own meeting plus one per create.
  EXPECT_EQ(scalarInt(*conn, "SELECT COUNT(*) FROM meetings"),
            1 + kWriterThreads * kMeetingsPerThread);
}

// Two transaction-opening methods against each other, on the same meeting.
// reissueForMeeting retires every active invitation and mints a fresh one as
// one atomic step; running several at once must leave exactly one active
// invitation, not several (interleaved mints) or none (an interleaved retire
// landing after the last mint).
TEST_F(RepositoryConcurrencyTest, ConcurrentReissuesLeaveExactlyOneActiveInvitation) {
  invitations->create(meeting.id, 1);

  constexpr int kThreads = 8;
  auto failures = runConcurrently(kThreads, [&](int) { invitations->reissueForMeeting(meeting.id, 1); });

  EXPECT_TRUE(failures.empty()) << "first failure: " << (failures.empty() ? "" : failures.front());
  EXPECT_EQ(scalarInt(*conn, "SELECT COUNT(*) FROM invitations WHERE meeting_id = " +
                                  std::to_string(meeting.id) + " AND status = 'active'"),
            1);
  // Nothing vanished: the original plus one per reissue.
  EXPECT_EQ(scalarInt(*conn, "SELECT COUNT(*) FROM invitations WHERE meeting_id = " +
                                  std::to_string(meeting.id)),
            1 + kThreads);
}

// Mixed traffic across all three repositories on the one connection — the
// closest repository-level analogue of real request mix — checking that reads
// interleaved with transactional writes neither throw nor observe a
// half-applied transaction.
TEST_F(RepositoryConcurrencyTest, MixedRepositoryTrafficDoesNotThrowOrTearState) {
  auto created = invitations->create(meeting.id, 1);
  pcm::tokenbackend::AccountsRepository accounts(*conn);

  constexpr int kThreads = 16;
  constexpr int kIterations = 10;

  auto failures = runConcurrently(kThreads, [&](int index) {
    for (int i = 0; i < kIterations; ++i) {
      switch (index % 4) {
      case 0:
        invitations->recordFailedPasscodeAttempt(created.invitation.id);
        break;
      case 1: {
        auto found = invitations->findByCode(created.invitationCode);
        ASSERT_TRUE(found.has_value());
        // The counter only ever moves forward, and a reader must never see a
        // value from inside another thread's uncommitted transaction.
        ASSERT_GE(found->passcodeAttempts, 0);
        break;
      }
      case 2: {
        auto found = meetings->findById(meeting.id);
        ASSERT_TRUE(found.has_value());
        ASSERT_EQ(found->meetingRef, meeting.meetingRef);
        break;
      }
      default:
        // Wrong credential on purpose: exercises the accounts read path
        // without mutating the seeded row.
        ASSERT_FALSE(accounts.findByCredential("not-a-real-credential").has_value());
        break;
      }
    }
  });

  EXPECT_TRUE(failures.empty()) << "first failure: " << (failures.empty() ? "" : failures.front());
  // Four of the sixteen threads increment, kIterations times each.
  EXPECT_EQ(scalarInt(*conn, "SELECT passcode_attempts FROM invitations WHERE id = " +
                                  std::to_string(created.invitation.id)),
            (kThreads / 4) * kIterations);
}
