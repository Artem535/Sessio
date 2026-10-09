#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "speech_recognizer.h"
#include "voice_activity.h"

namespace pcm::transcription::testing {

// 48 kHz int16 audio: constant 16000 for speech, zero for silence.
inline std::vector<int16_t> speech(int ms) { return std::vector<int16_t>(48 * ms, 16000); }
inline std::vector<int16_t> silence(int ms) { return std::vector<int16_t>(48 * ms, 0); }

inline std::vector<int16_t> concat(std::initializer_list<std::vector<int16_t>> parts) {
  std::vector<int16_t> out;
  for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
  return out;
}

// Speech when the window's mean absolute value exceeds 0.05; a phrase closes on
// the first window that is not speech.
class FakeVad : public IVoiceActivityDetector {
 public:
  size_t windowSize() const override { return 512; }

  void accept(const float* window, size_t n) override {
    double sum = 0;
    for (size_t i = 0; i < n; ++i) sum += std::fabs(window[i]);
    const bool is_speech = sum / static_cast<double>(n) > 0.05;
    if (is_speech) {
      if (!open_) {
        open_ = true;
        current_.start_sample = fed_;
        current_.samples.clear();
      }
      current_.samples.insert(current_.samples.end(), window, window + n);
    } else if (open_) {
      close();
    }
    fed_ += static_cast<int64_t>(n);
  }

  bool hasSegment() const override { return !ready_.empty(); }

  SpeechSegment popSegment() override {
    SpeechSegment s = std::move(ready_.front());
    ready_.pop_front();
    return s;
  }

  void flush() override {
    if (open_) close();
  }

 private:
  void close() {
    ready_.push_back(std::move(current_));
    current_ = {};
    open_ = false;
  }

  bool open_ = false;
  int64_t fed_ = 0;
  SpeechSegment current_;
  std::deque<SpeechSegment> ready_;
};

class FakeRecognizer : public ISpeechRecognizer {
 public:
  using Handler = std::function<std::string(int call, size_t samples)>;

  explicit FakeRecognizer(Handler handler = {}) : handler_(std::move(handler)) {}

  std::string transcribe(std::span<const float> samples) override {
    const int call = calls_++;
    if (handler_) return handler_(call, samples.size());
    return "phrase " + std::to_string(call);
  }

  int calls() const { return calls_; }

 private:
  Handler handler_;
  std::atomic<int> calls_{0};
};

template <typename Pred>
bool waitFor(Pred pred, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return pred();
}

}  // namespace pcm::transcription::testing
