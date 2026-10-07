#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace pcm::calltranscription {

// One worker thread running queued tasks in order. The destructor drains the
// queue, then joins. A throwing task is contained and does not stop the worker.
class SerialExecutor {
public:
  SerialExecutor() : thread_([this] { run(); }) {}

  ~SerialExecutor() {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
  }

  SerialExecutor(const SerialExecutor &) = delete;
  SerialExecutor &operator=(const SerialExecutor &) = delete;

  void post(std::function<void()> task) {
    {
      std::lock_guard lock(mutex_);
      queue_.push_back(std::move(task));
    }
    cv_.notify_one();
  }

  // Blocks until every task posted so far has finished.
  void waitIdle() {
    std::unique_lock lock(mutex_);
    idle_cv_.wait(lock, [this] { return queue_.empty() && !running_; });
  }

private:
  void run() {
    for (;;) {
      std::function<void()> task;
      {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
        if (queue_.empty()) return;  // stopping and drained
        task = std::move(queue_.front());
        queue_.pop_front();
        running_ = true;
      }
      try {
        task();
      } catch (...) {
      }
      task = nullptr;
      {
        std::lock_guard lock(mutex_);
        running_ = false;
      }
      idle_cv_.notify_all();
    }
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  std::condition_variable idle_cv_;
  std::deque<std::function<void()>> queue_;
  bool stopping_ = false;
  bool running_ = false;
  std::thread thread_;  // last: starts after the other members are built
};

} // namespace pcm::calltranscription
