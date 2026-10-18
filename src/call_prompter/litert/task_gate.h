#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace sessio::litert::detail {

// Cancellation must cover task submission as well as execution: LiteRT's
// session-wide CancelProcess() does not cancel tasks submitted afterwards.
class TaskGate {
public:
  class Task {
  public:
    virtual ~Task() = default;
    virtual bool wait() = 0;
    virtual void cancel() = 0;
  };
  enum class Result { Done, Cancelled, Failed };

  bool beginGeneration() {
    std::lock_guard lock(mMutex);
    return !std::exchange(mCancelled, false);
  }

  Result run(const std::function<std::shared_ptr<Task>()> &submit) {
    std::shared_ptr<Task> task;
    {
      std::lock_guard lock(mMutex);
      if (mCancelled) return Result::Cancelled;
      task = submit();
      if (!task) return Result::Failed;
      mActive = task;
    }
    bool ok;
    try { ok = task->wait(); }
    catch (...) {
      std::lock_guard lock(mMutex);
      mActive.reset();
      throw;
    }
    std::lock_guard lock(mMutex);
    mActive.reset();
    return mCancelled ? Result::Cancelled : ok ? Result::Done : Result::Failed;
  }

  void cancel() {
    std::lock_guard lock(mMutex);
    mCancelled = true;
    if (mActive) mActive->cancel();
  }

private:
  std::mutex mMutex;
  std::shared_ptr<Task> mActive;
  bool mCancelled = false;
};

} // namespace sessio::litert::detail
