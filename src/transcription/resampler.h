#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace pcm::transcription {

// Converts mono int16 audio at a multiple of 16 kHz (the call delivers 48 kHz)
// to 16 kHz float for the recogniser. Stateful: feed consecutive chunks of one
// stream; chunk boundaries do not change the output.
class Resampler {
 public:
  explicit Resampler(int input_rate);

  std::vector<float> process(std::span<const int16_t> input);

 private:
  int ratio_;
  std::vector<float> taps_;
  std::vector<float> history_;  // last taps_.size() - 1 input samples
  size_t next_ = 0;             // index in the next input of the next output sample
};

}  // namespace pcm::transcription
