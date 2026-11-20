#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pcm::transcription {

struct SpeechSegment {
  int64_t start_sample = 0;  // 16 kHz samples since the stream started
  std::vector<float> samples;
};

// Streaming voice-activity detector. One instance per track.
class IVoiceActivityDetector {
 public:
  virtual ~IVoiceActivityDetector() = default;

  virtual size_t windowSize() const = 0;
  // Exactly windowSize() samples at 16 kHz.
  virtual void accept(const float* window, size_t n) = 0;
  virtual bool hasSegment() const = 0;
  virtual SpeechSegment popSegment() = 0;
  // Closes a phrase that is still open (end of stream).
  virtual void flush() = 0;
};

}  // namespace pcm::transcription
