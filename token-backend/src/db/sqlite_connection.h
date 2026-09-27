#pragma once

#include <sqlite3.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace pcm::tokenbackend {

// One connection is shared by every HTTP request handler (see main.cpp), and
// oat++'s HttpConnectionHandler dispatches connections across worker threads,
// so this handle is used concurrently. The constructor configures the
// connection for that explicitly rather than relying on how the SQLite library
// happened to be compiled — see sqlite_connection.cpp.
class SqliteConnection {
public:
  explicit SqliteConnection(const std::string &path);
  ~SqliteConnection();

  SqliteConnection(const SqliteConnection &) = delete;
  SqliteConnection &operator=(const SqliteConnection &) = delete;

  sqlite3 *raw() const { return db_; }
  void exec(const std::string &sql);

  // Recursive because the repository layer's entry points call one another:
  // MeetingsRepository::create() reads its own row back through findByRef(),
  // InvitationsRepository::reissueForMeeting() calls invalidateAllForMeeting()
  // and create(), recordFailedPasscodeAttempt() calls invalidate(). Every one
  // of those is a public entry point that must take the lock when it is called
  // from outside, so the same thread re-enters the lock on the nested call. A
  // plain std::mutex would deadlock; duplicating each method as a private
  // already-locked twin would double the repository surface for no gain.
  //
  // The recursive mutex has one cost, which is why this is a class rather than
  // a bare std::unique_lock: it makes a mis-nested transaction *silent*. Under
  // a plain mutex, wrapping a method that already opens a transaction in
  // another transaction would deadlock loudly on the first call. Here it
  // compiles, runs, and only fails in production under real concurrency with
  // the same SQLITE_ERROR this lock exists to eliminate. So this tracks which
  // thread holds it and how deep, and SqliteTransaction checks that — see
  // heldByCurrentThread() and SqliteTransaction's constructor.
  class Lock {
  public:
    explicit Lock(SqliteConnection &conn) : conn_(conn), guard_(conn.mutex_) {
      // Both writes happen with the mutex held, so the depth count needs no
      // synchronization of its own. The owner id is atomic only because
      // heldByCurrentThread() is by definition also called by threads that do
      // NOT hold the lock — that is the case it exists to catch.
      conn_.lockOwner_.store(std::this_thread::get_id(), std::memory_order_release);
      ++conn_.lockDepth_;
    }

    ~Lock() {
      // Ordered before guard_'s release, so the state is never observed stale.
      if (--conn_.lockDepth_ == 0) {
        conn_.lockOwner_.store(std::thread::id{}, std::memory_order_release);
      }
    }

    // Non-copyable and non-movable. lock() can still return one by value:
    // C++17 guarantees the prvalue is constructed directly in the caller's
    // storage, so no move is involved.
    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;

  private:
    SqliteConnection &conn_;
    std::unique_lock<std::recursive_mutex> guard_;
  };

  // Serializes ALL database access. Returns a lock that is already held, so a
  // caller writes `auto guard = conn_.lock();` and gets release-on-return,
  // including when an exception unwinds the frame.
  //
  // SQLITE_OPEN_FULLMUTEX (see the constructor) only serializes individual
  // SQLite API calls. It does not make a *sequence* of them atomic, so without
  // this lock two failure modes are live on the shared connection:
  //
  //  1. A second thread issuing BEGIN IMMEDIATE while another thread's
  //     transaction is open gets SQLITE_ERROR ("cannot start a transaction
  //     within a transaction"), not SQLITE_BUSY — the busy timeout does not
  //     help, and the repository turns it into a 500.
  //  2. Worse, a statement issued outside any transaction (e.g.
  //     MeetingsRepository::create()'s INSERT) can land inside another
  //     thread's open transaction and be silently undone if that transaction
  //     later rolls back — after its own caller was told it succeeded.
  //
  // Every public repository method that touches the database therefore holds
  // this for its whole body, which makes at most one such call in flight at a
  // time and puts both failure modes structurally out of reach. The cost is
  // that database access is serialized process-wide; for a single-practitioner
  // backend that is not a meaningful constraint.
  //
  // Code that reaches the connection directly rather than through a repository
  // — runMigrations() is the only case — runs at startup before any request
  // thread exists and does not take this.
  [[nodiscard]] Lock lock() { return Lock(*this); }

  // True when the calling thread currently holds lock(). Lets a precondition be
  // checked instead of merely documented — see SqliteTransaction's constructor.
  [[nodiscard]] bool heldByCurrentThread() const {
    return lockOwner_.load(std::memory_order_acquire) == std::this_thread::get_id();
  }

private:
  friend class Lock;
  friend class SqliteTransaction;

  // Only ever read or written by a thread that holds the lock, so a plain bool
  // is sufficient: the one caller that may not hold it (SqliteTransaction's
  // constructor) checks heldByCurrentThread() first and throws before it gets
  // here.
  bool transactionOpen_ = false;

  sqlite3 *db_ = nullptr;
  std::recursive_mutex mutex_;
  std::atomic<std::thread::id> lockOwner_{};
  int lockDepth_ = 0;
};

// RAII wrapper over BEGIN IMMEDIATE / COMMIT / ROLLBACK.
//
// Serialized threading mode makes each *statement* safe against concurrent
// use of the connection; it does not make a read-modify-write *sequence*
// atomic. Any repository method that issues more than one statement and whose
// intermediate state would be wrong to observe (or to leave behind on a
// failure) opens one of these.
//
// BEGIN IMMEDIATE rather than plain BEGIN: the write lock is taken up front,
// so two concurrent transactions cannot both read and then deadlock trying to
// upgrade. Rolls back on destruction unless commit() was called, so an
// exception thrown mid-sequence cannot leave a partial write behind.
//
// Two preconditions, both CHECKED by the constructor rather than just
// documented — it throws std::logic_error naming the one that was violated:
//
//  1. The caller already holds SqliteConnection::lock(), and keeps holding it
//     until this object is destroyed. Otherwise another thread's statement can
//     land between this transaction's BEGIN and its COMMIT, which is the whole
//     failure this lock exists to prevent.
//  2. No transaction is already open on this connection. SQLite has no nested
//     transactions, and because the lock is recursive it will not stop a method
//     that opens a transaction from being called inside another one — that
//     mistake would otherwise compile, run, and only surface in production as
//     the SQLITE_ERROR this fix eliminated.
//
// Checked unconditionally, not behind assert(): asserts vanish in release
// builds, and a violation is a 500 on a live endpoint. Two comparisons next to
// a BEGIN IMMEDIATE cost nothing measurable.
class SqliteTransaction {
public:
  explicit SqliteTransaction(SqliteConnection &conn);
  ~SqliteTransaction();

  SqliteTransaction(const SqliteTransaction &) = delete;
  SqliteTransaction &operator=(const SqliteTransaction &) = delete;

  void commit();

private:
  SqliteConnection &conn_;
  bool done_ = false;
};

} // namespace pcm::tokenbackend
