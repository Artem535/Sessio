#pragma once

#include <QString>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

namespace pcm::video {

// Receives call audio from any capture/reader thread. Implementations must be
// thread-safe and cheap: the call happens on an audio thread.
class AudioSink {
public:
  virtual ~AudioSink() = default;
  // May be called from any thread. `samples` is mono int16 valid only for the call.
  virtual void onAudio(const QString &participantId, const int16_t *samples,
                       std::size_t count, int sampleRate) = 0;
};

// Thread-safe holder of the current sink, shared by the provider and its taps.
class AudioSinkSlot {
public:
  void set(std::shared_ptr<AudioSink> sink) {
    std::lock_guard lock(mMutex);
    mSink = std::move(sink);
  }
  [[nodiscard]] std::shared_ptr<AudioSink> get() const {
    std::lock_guard lock(mMutex);
    return mSink;
  }

private:
  mutable std::mutex mMutex;
  std::shared_ptr<AudioSink> mSink;
};

// One per audio source. push() is lock-light and thread-safe; the callback is
// copied out under the lock and invoked outside it.
class AudioTap {
public:
  using Callback = std::function<void(const int16_t *, std::size_t, int)>;

  void setCallback(Callback cb) {
    std::shared_ptr<const Callback> next;
    if (cb)
      next = std::make_shared<const Callback>(std::move(cb));
    std::lock_guard lock(mMutex);
    mCallback = std::move(next);
  }
  void setEnabled(bool enabled) { mEnabled.store(enabled); }

  void push(const int16_t *samples, std::size_t count, int sampleRate) const {
    if (!mEnabled.load())
      return;
    std::shared_ptr<const Callback> cb;
    {
      std::lock_guard lock(mMutex);
      cb = mCallback;
    }
    if (cb)
      (*cb)(samples, count, sampleRate);
  }

private:
  mutable std::mutex mMutex;
  std::shared_ptr<const Callback> mCallback;
  std::atomic<bool> mEnabled{true};
};

} // namespace pcm::video
