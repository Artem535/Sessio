#include "phrase_writer.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace pcm::calltranscription {

PhraseWriter::PhraseWriter(Store store, Written on_written)
    : store_(std::move(store)), on_written_(std::move(on_written)) {
  thread_ = std::thread([this] { run(); });
}

PhraseWriter::~PhraseWriter() { stop(std::chrono::milliseconds(0)); }

void PhraseWriter::submit(DuckTranscriptPhrase phrase) {
  {
    std::lock_guard lock(mutex_);
    if (stopping_ || queue_.size() >= kMaxPending) {
      ++stats_.dropped;
      return;
    }
    queue_.push_back(std::move(phrase));
  }
  cv_.notify_all();  // worker and a draining stop() share cv_
}

void PhraseWriter::stop(std::chrono::milliseconds drain_timeout) {
  assert(std::this_thread::get_id() != thread_.get_id() &&
         "PhraseWriter::stop() must not be called from Store/Written");
  drain_timeout = std::clamp(drain_timeout, std::chrono::milliseconds(0),
                             std::chrono::milliseconds(std::chrono::hours(1)));
  std::lock_guard stop_lock(stop_mutex_);
  {
    std::unique_lock lock(mutex_);
    stopping_ = true;
    cv_.notify_all();
    cv_.wait_for(lock, drain_timeout, [this] { return queue_.empty(); });
    if (!queue_.empty()) {
      stats_.dropped += queue_.size();
      queue_.clear();
    }
    abort_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

WriterStats PhraseWriter::stats() const {
  std::lock_guard lock(mutex_);
  return stats_;
}

void PhraseWriter::run() {
  for (;;) {
    DuckTranscriptPhrase phrase;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] { return !queue_.empty() || stopping_; });
      if (abort_ || queue_.empty()) return;
      phrase = std::move(queue_.front());
      queue_.pop_front();
    }

    int64_t id = 0;
    bool ok = false;
    try {
      id = store_(phrase);
      ok = id > 0;
    } catch (...) {
    }

    bool callback_failed = false;
    if (ok && on_written_) {
      phrase.id = id;
      try {
        on_written_(phrase);
      } catch (...) {
        callback_failed = true;
      }
    }

    {
      std::lock_guard lock(mutex_);
      if (ok) ++stats_.written;
      if (!ok) ++stats_.failed;
      if (callback_failed) ++stats_.callback_failed;
      if (queue_.empty()) cv_.notify_all();  // wake a draining stop()
    }
  }
}

}  // namespace pcm::calltranscription
