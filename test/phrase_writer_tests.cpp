#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
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
  void wait() {
    std::unique_lock lock(m);
    cv.wait_for(lock, kWait, [&] { return open; });
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

}  // namespace

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
        r.cv.notify_all();
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
  EXPECT_EQ(s.failed, 2u);   // one throwing store, one throwing callback
  EXPECT_EQ(s.written, 3u);  // the throwing-callback row was already stored
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
        release.wait();
        return int64_t{1};
      },
      nullptr);
  for (int i = 0; i < 5; ++i) w.submit(phrase(i));
  entered.wait();  // first item is in Store; four remain queued

  std::thread stopper([&] { w.stop(0ms); });
  // stop() must not return before the in-flight Store finishes (join).
  std::this_thread::sleep_for(50ms);
  release.release();
  stopper.join();

  EXPECT_EQ(stored.load(), 1);
  const auto s = w.stats();
  EXPECT_EQ(s.written, 1u);
  EXPECT_EQ(s.dropped, 4u);
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
