#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include "schema.hpp"

namespace pcm::calltranscription {

struct WriterStats {
  uint64_t written = 0;
  uint64_t failed = 0;
  uint64_t dropped = 0;
};

// Persists phrases on one dedicated thread, in submission order, so the decode
// thread and the GUI thread never wait on the database. Qt-free.
class PhraseWriter {
 public:
  // Returns the new row id, or <= 0 on failure. Runs on the writer thread.
  using Store = std::function<int64_t(const DuckTranscriptPhrase &)>;
  // Called with the stored row (id assigned) on the writer thread.
  using Written = std::function<void(const DuckTranscriptPhrase &)>;

  PhraseWriter(Store store, Written on_written);
  ~PhraseWriter();  // equivalent to stop(0 ms)

  PhraseWriter(const PhraseWriter &) = delete;
  PhraseWriter &operator=(const PhraseWriter &) = delete;

  // Any thread, non-blocking. After stop() the phrase is counted as dropped.
  void submit(DuckTranscriptPhrase phrase);
  // Idempotent. Waits up to drain_timeout for the queue to empty, drops the
  // rest and joins the thread; Store is never invoked after this returns.
  void stop(std::chrono::milliseconds drain_timeout);
  WriterStats stats() const;

 private:
  void run();

  Store store_;
  Written on_written_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<DuckTranscriptPhrase> queue_;
  WriterStats stats_;
  bool stopping_ = false;
  bool abort_ = false;
  std::mutex stop_mutex_;  // serialises concurrent stop() callers around join
  std::thread thread_;
};

}  // namespace pcm::calltranscription
