#pragma once

#include <QString>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

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
    mHasSink.store(mSink != nullptr, std::memory_order_relaxed);
  }
  // Lock-free hint for hot paths; get() is authoritative.
  [[nodiscard]] bool hasSink() const { return mHasSink.load(std::memory_order_relaxed); }
  [[nodiscard]] std::shared_ptr<AudioSink> get() const {
    std::lock_guard lock(mMutex);
    return mSink;
  }

private:
  mutable std::mutex mMutex;
  std::shared_ptr<AudioSink> mSink;
  std::atomic<bool> mHasSink{false};
};

// Averages interleaved frames into mono. A trailing partial frame is ignored.
inline void downmixToMono(const int16_t *interleaved, std::size_t frames, int channels,
                          std::vector<int16_t> &out) {
  out.clear();
  if (channels <= 0) {
    return;
  }
  if (channels == 1) {
    out.assign(interleaved, interleaved + frames);
    return;
  }
  const auto ch = static_cast<std::size_t>(channels);
  out.resize(frames);
  for (std::size_t i = 0; i < frames; ++i) {
    int sum = 0;
    for (std::size_t c = 0; c < ch; ++c) {
      sum += interleaved[i * ch + c];
    }
    out[i] = static_cast<int16_t>(sum / channels);
  }
}

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
    mHasCallback.store(mCallback != nullptr, std::memory_order_relaxed);
  }
  // Mute gate (e.g. the real microphone state). Independent of setSinkActive().
  void setEnabled(bool enabled) { mEnabled.store(enabled); }
  // Listener gate (is any sink installed). Never overrides setEnabled(false).
  void setSinkActive(bool active) { mSinkActive.store(active, std::memory_order_relaxed); }

  // Cheap check so producers can skip downmix/copy work nobody will consume.
  [[nodiscard]] bool active() const {
    return mHasCallback.load(std::memory_order_relaxed) && mEnabled.load() &&
           mSinkActive.load(std::memory_order_relaxed);
  }

  void push(const int16_t *samples, std::size_t count, int sampleRate) const {
    if (!active()) {
      return;
    }
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
  std::atomic<bool> mSinkActive{true};
  std::atomic<bool> mHasCallback{false};
};

} // namespace pcm::video
