#pragma once

// Throwaway spike for issue #77 — not production quality.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace pcm::spike {

// Accumulates interleaved int16 PCM samples and yields fixed-size frames.
// Framework-free (no Qt, no LiveKit) so it can be unit-tested directly.
class AudioChunker {
public:
  AudioChunker(std::size_t samplesPerChannel, int channels);

  // Appends newSamples to the internal buffer and returns every complete
  // frame that can now be formed. Any leftover samples are kept for the
  // next call.
  [[nodiscard]] std::vector<std::vector<int16_t>> push(
      const std::vector<int16_t> &newSamples);

  // Discards any partial-frame remainder buffered so far. Call this when the
  // underlying audio stream restarts (e.g. switching capture devices) so a
  // leftover remainder from the old stream isn't prepended to the new one.
  void reset();

private:
  std::size_t mFrameSize;  // samplesPerChannel * channels
  std::deque<int16_t> mBuffer;
};

}  // namespace pcm::spike
