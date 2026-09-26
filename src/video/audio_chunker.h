#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace pcm::video {

// Accumulates interleaved PCM samples and emits fixed-size frames as soon as
// enough samples have arrived. Framework-free (no Qt, no LiveKit) so it is
// unit-testable without a running application.
class AudioChunker {
public:
  AudioChunker(std::size_t samplesPerChannel, int channels);

  [[nodiscard]] std::vector<std::vector<int16_t>>
  push(const std::vector<int16_t> &newSamples);

  // Discards any partial-frame remainder. Call this when switching capture
  // devices so stale samples from the old device are never prepended to the
  // new device's stream.
  void reset();

private:
  std::size_t mFrameSize;
  std::deque<int16_t> mBuffer;
};

} // namespace pcm::video
