#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

#include "phrase_writer.h"

using namespace std::chrono_literals;
using pcm::calltranscription::PhraseWriter;

namespace {

constexpr auto kWait = 10s;

DuckTranscriptPhrase phrase(int n) {
  DuckTranscriptPhrase p;
  p.transcript_id = 1;
  p.track_role = "participant";
  p.start_ms = n;
  p.end_ms = n + 1;
  p.text = "p" + std::to_string(n);
  return p;
}

// Waits until `pred` holds, bounded by kWait.
template <class Pred>
bool waitFor(std::mutex &m, std::condition_variable &cv, Pred pred) {
  std::unique_lock lock(m);
  return cv.wait_for(lock, kWait, pred);
}

struct Latch {
  std::mutex m;
  std::condition_variable cv;
  bool open = false;
  void release() {
    {
      std::lock_guard lock(m);
      open = true;
    }
    cv.notify_all();
  }
  bool wait() {
    std::unique_lock lock(m);
    return cv.wait_for(lock, kWait, [&] { return open; });
  }
};

struct Recorder {
  std::mutex m;
  std::condition_variable cv;
  std::vector<int64_t> start_ms;
  std::vector<int64_t> written_ids;
  std::set<std::thread::id> threads;
  int64_t next_id = 100;
};

// Polls (bounded) until `pred` holds.
template <class Pred>
bool pollUntil(Pred pred) {
  const auto deadline = std::chrono::steady_clock::now() + kWait;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(1ms);
  }
  return pred();
}

} // namespace

TEST(PhraseWriter, WritesInSubmissionOrder) {
  Recorder r;
  {
    PhraseWriter w(
        [&](const DuckTranscriptPhrase &p) {
          std::lock_guard lock(r.m);
          r.start_ms.push_back(p.start_ms);
          r.cv.notify_all();
          return r.next_id++;
        },
        nullptr);
    for (int i = 0; i < 50; ++i) w.submit(phrase(i));
    ASSERT_TRUE(waitFor(r.m, r.cv, [&] { return r.start_ms.size() == 50; }));
    w.stop(kWait);
    EXPECT_EQ(w.stats().written, 50u);
  }
  for (int i = 0; i < 50; ++i) EXPECT_EQ(r.start_ms[i], i);
}

TEST(PhraseWriter, StoreRunsOffTheCallingThread) {
  Recorder r;
  PhraseWriter w(
      [&](const DuckTranscriptPhrase &) {
        std::lock_guard lock(r.m);
        r.threads.insert(std::this_thread::get_id());
        return int64_t{1};
      },
      nullptr);
  for (int i = 0; i < 5; ++i) w.submit(phrase(i));
  w.stop(kWait);
  ASSERT_EQ(r.threads.size(), 1u);
  EXPECT_NE(*r.threads.begin(), std::this_thread::get_id());
}

TEST(PhraseWriter, FailedStoreIsCountedAndDoesNotStopTheWriter) {
  std::atomic<int> calls{0};
  PhraseWriter w(
      [&](const DuckTranscriptPhrase &) { return calls++ == 0 ? int64_t{0} : int64_t{7}; },
      nullptr);
  w.submit(phrase(0));
  w.submit(phrase(1));
  w.submit(phrase(2));
  w.stop(kWait);
  const auto s = w.stats();
  EXPECT_EQ(s.failed, 1u);
  EXPECT_EQ(s.written, 2u);
  EXPECT_EQ(s.dropped, 0u);
}

TEST(PhraseWriter, ThrowingStoreAndThrowingCallbackAreContained) {
  std::atomic<int> calls{0};
  std::atomic<int> callbacks{0};
  PhraseWriter w(
      [&](const DuckTranscriptPhrase &) -> int64_t {
        if (calls++ == 0) throw std::runtime_error("store");
        return 5;
      },
      [&](const DuckTranscriptPhrase &) {
        if (callbacks++ == 0) throw std::runtime_error("callback");
      });
  for (int i = 0; i < 4; ++i) w.submit(phrase(i));
  w.stop(kWait);
  const auto s = w.stats();
  EXPECT_EQ(calls.load(), 4);
  EXPECT_EQ(s.failed, 1u);           // the throwing store only
  EXPECT_EQ(s.callback_failed, 1u);  // the throwing callback only
  EXPECT_EQ(s.written, 3u);          // the throwing-callback row was already stored
  EXPECT_EQ(callbacks.load(), 3);
}

TEST(PhraseWriter, StopDrainsQueuedItems) {
  std::atomic<int> stored{0};
  PhraseWriter w([&](const DuckTranscriptPhrase &) { return int64_t{++stored}; }, nullptr);
  for (int i = 0; i < 200; ++i) w.submit(phrase(i));
  w.stop(kWait);
  EXPECT_EQ(stored.load(), 200);
  EXPECT_EQ(w.stats().written, 200u);
  EXPECT_EQ(w.stats().dropped, 0u);
}

TEST(PhraseWriter, StopWithZeroTimeoutDropsPendingAndCountsThem) {
  Latch entered, release;
  std::atomic<int> stored{0};
  PhraseWriter w(
      [&](const DuckTranscriptPhrase &) {
        ++stored;
        entered.release();
        EXPECT_TRUE(release.wait());
        return int64_t{1};
      },
      nullptr);
  for (int i = 0; i < 5; ++i) w.submit(phrase(i));
  ASSERT_TRUE(entered.wait());  // first item is in Store; four remain queued

  std::atomic<bool> stopReturned{false};
  std::thread stopper([&] {
    w.stop(0ms);
    stopReturned = true;
  });
  ASSERT_TRUE(pollUntil([&] { return w.stats().dropped == 4; }));
  // stop() must not return before the in-flight Store finishes (join).
  EXPECT_FALSE(stopReturned.load());
  release.release();
  stopper.join();
  EXPECT_TRUE(stopReturned.load());

  EXPECT_EQ(stored.load(), 1);
  const auto s = w.stats();
  EXPECT_EQ(s.written, 1u);
  EXPECT_EQ(s.dropped, 4u);
}

TEST(PhraseWriter, NonZeroTimeoutExpiringWithBlockedStoreDropsTheRest) {
  Latch blocked, release;
  std::atomic<int> calls{0};
  PhraseWriter w(
      [&](const DuckTranscriptPhrase &) {
        if (calls++ == 2) {
          blocked.release();
          EXPECT_TRUE(release.wait());
        }
        return int64_t{1};
      },
      nullptr);
  for (int i = 0; i < 5; ++i) w.submit(phrase(i));
  ASSERT_TRUE(blocked.wait());  // items 0,1 stored; 2 in Store; 3,4 queued

  std::atomic<bool> stopReturned{false};
  std::thread stopper([&] {
    w.stop(100ms);
    stopReturned = true;
  });
  ASSERT_TRUE(pollUntil([&] { return w.stats().dropped == 2; }));
  EXPECT_FALSE(stopReturned.load());
  release.release();
  stopper.join();

  const auto s = w.stats();
  EXPECT_EQ(calls.load(), 3);
  EXPECT_EQ(s.written, 3u);
  EXPECT_EQ(s.dropped, 2u);
}

TEST(PhraseWriter, QueueIsCappedAndOverflowIsCountedAsDropped) {
  Latch entered, release;
  PhraseWriter w(
      [&](const DuckTranscriptPhrase &) {
        entered.release();
        EXPECT_TRUE(release.wait());
        return int64_t{1};
      },
      nullptr);
  w.submit(phrase(0));
  ASSERT_TRUE(entered.wait());
  for (int i = 1; i <= 10010; ++i) w.submit(phrase(i));
  EXPECT_GE(w.stats().dropped, 10u);
  release.release();
  w.stop(kWait);
  const auto s = w.stats();
  EXPECT_EQ(s.written + s.dropped, 10011u);
  EXPECT_EQ(s.written, 1u + PhraseWriter::kMaxPending);
}

TEST(PhraseWriter, ConcurrentStopFromTwoThreadsIsSafe) {
  std::atomic<int> stored{0};
  PhraseWriter w([&](const DuckTranscriptPhrase &) { return int64_t{++stored}; }, nullptr);
  for (int i = 0; i < 200; ++i) w.submit(phrase(i));
  std::atomic<bool> go{false};
  auto stopFn = [&] {
    while (!go) std::this_thread::yield();
    w.stop(kWait);
  };
  std::thread a(stopFn), b(stopFn);
  go = true;
  a.join();
  b.join();
  const auto s = w.stats();
  EXPECT_EQ(s.written + s.dropped, 200u);
  EXPECT_EQ(s.written, 200u);
}

TEST(PhraseWriter, DestructorWithPendingQueueDoesNotHang) {
  auto fut = std::async(std::launch::async, [] {
    PhraseWriter w(
        [](const DuckTranscriptPhrase &) {
          std::this_thread::sleep_for(2ms);
          return int64_t{1};
        },
        nullptr);
    for (int i = 0; i < 500; ++i) w.submit(phrase(i));
  });
  EXPECT_EQ(fut.wait_for(kWait), std::future_status::ready);
}

TEST(PhraseWriter, StopZeroOnEmptyQueueReturnsAndDropsNothing) {
  PhraseWriter w([](const DuckTranscriptPhrase &) { return int64_t{1}; }, nullptr);
  w.stop(0ms);
  EXPECT_EQ(w.stats().dropped, 0u);
  EXPECT_EQ(w.stats().written, 0u);
}

TEST(PhraseWriter, WrittenIsNotInvokedWhenStoreFails) {
  std::atomic<int> callbacks{0};
  PhraseWriter w([](const DuckTranscriptPhrase &) { return int64_t{0}; },
                 [&](const DuckTranscriptPhrase &) { ++callbacks; });
  for (int i = 0; i < 3; ++i) w.submit(phrase(i));
  w.stop(kWait);
  EXPECT_EQ(callbacks.load(), 0);
  EXPECT_EQ(w.stats().failed, 3u);
  EXPECT_EQ(w.stats().written, 0u);
}

TEST(PhraseWriter, SubmitAfterStopIsDropped) {
  std::atomic<int> stored{0};
  PhraseWriter w([&](const DuckTranscriptPhrase &) { return int64_t{++stored}; }, nullptr);
  w.stop(kWait);
  w.submit(phrase(1));
  w.submit(phrase(2));
  EXPECT_EQ(stored.load(), 0);
  EXPECT_EQ(w.stats().dropped, 2u);
  EXPECT_EQ(w.stats().written, 0u);
}

TEST(PhraseWriter, StopIsIdempotent) {
  PhraseWriter w([](const DuckTranscriptPhrase &) { return int64_t{1}; }, nullptr);
  w.submit(phrase(0));
  w.stop(kWait);
  w.stop(kWait);
  w.stop(0ms);
  EXPECT_EQ(w.stats().written, 1u);
  EXPECT_EQ(w.stats().dropped, 0u);
}

TEST(PhraseWriter, WrittenCallbackReceivesAssignedId) {
  Recorder r;
  PhraseWriter w(
      [&](const DuckTranscriptPhrase &) { return int64_t{42}; },
      [&](const DuckTranscriptPhrase &p) {
        std::lock_guard lock(r.m);
        r.written_ids.push_back(p.id);
        r.start_ms.push_back(p.start_ms);
        r.cv.notify_all();
      });
  w.submit(phrase(9));
  ASSERT_TRUE(waitFor(r.m, r.cv, [&] { return !r.written_ids.empty(); }));
  w.stop(kWait);
  EXPECT_EQ(r.written_ids[0], 42);
  EXPECT_EQ(r.start_ms[0], 9);
}
