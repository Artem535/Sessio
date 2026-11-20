#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace pcm::calltranscription {

// One worker thread running queued tasks in order. The destructor drains the
// queue, then joins. A throwing task is contained and does not stop the worker.
//
// Tasks may own the object that owns this executor, so the executor can be
// destroyed on its own thread while a finished task is released. The worker
// therefore keeps the queue state alive itself, and a destructor running on
// the worker thread detaches instead of joining (a self-join throws and
// terminates the process).
class SerialExecutor {
public:
  SerialExecutor() : state_(std::make_shared<State>()) {
    thread_ = std::thread([state = state_] { run(*state); });
  }

  ~SerialExecutor() {
    {
      std::lock_guard lock(state_->mutex);
      state_->stopping = true;
    }
    state_->cv.notify_all();
    if (!thread_.joinable()) return;
    if (thread_.get_id() == std::this_thread::get_id())
      thread_.detach();  // the worker exits on its own once the queue is drained
    else
      thread_.join();
  }

  SerialExecutor(const SerialExecutor &) = delete;
  SerialExecutor &operator=(const SerialExecutor &) = delete;

  void post(std::function<void()> task) {
    {
      std::lock_guard lock(state_->mutex);
      state_->queue.push_back(std::move(task));
    }
    state_->cv.notify_one();
  }

  // Blocks until every task posted so far has finished.
  void waitIdle() {
    std::unique_lock lock(state_->mutex);
    state_->idle_cv.wait(lock, [this] { return state_->queue.empty() && !state_->running; });
  }

private:
  struct State {
    std::mutex mutex;
    std::condition_variable cv;
    std::condition_variable idle_cv;
    std::deque<std::function<void()>> queue;
    bool stopping = false;
    bool running = false;
  };

  // Touches only `state`, never the SerialExecutor, which may already be gone.
  static void run(State &state) {
    for (;;) {
      std::function<void()> task;
      {
        std::unique_lock lock(state.mutex);
        state.cv.wait(lock, [&state] { return state.stopping || !state.queue.empty(); });
        if (state.queue.empty()) return;  // stopping and drained
        task = std::move(state.queue.front());
        state.queue.pop_front();
        state.running = true;
      }
      try {
        task();
      } catch (...) {
      }
      // May drop the last reference to this executor's owner (see above).
      task = nullptr;
      {
        std::lock_guard lock(state.mutex);
        state.running = false;
      }
      state.idle_cv.notify_all();
    }
  }

  std::shared_ptr<State> state_;
  std::thread thread_;
};

} // namespace pcm::calltranscription
