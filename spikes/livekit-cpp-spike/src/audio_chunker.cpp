// Throwaway spike for issue #77 — not production quality.
#include "audio_chunker.h"

namespace pcm::spike {

AudioChunker::AudioChunker(const std::size_t samplesPerChannel,
                           const int channels)
    : mFrameSize(samplesPerChannel * static_cast<std::size_t>(channels)) {}

std::vector<std::vector<int16_t>> AudioChunker::push(
    const std::vector<int16_t> &newSamples) {
  mBuffer.insert(mBuffer.end(), newSamples.begin(), newSamples.end());

  std::vector<std::vector<int16_t>> frames;
  while (mBuffer.size() >= mFrameSize) {
    std::vector<int16_t> frame(mBuffer.begin(), mBuffer.begin() + static_cast<std::ptrdiff_t>(mFrameSize));
    frames.push_back(std::move(frame));
    mBuffer.erase(mBuffer.begin(), mBuffer.begin() + static_cast<std::ptrdiff_t>(mFrameSize));
  }
  return frames;
}

void AudioChunker::reset() { mBuffer.clear(); }

}  // namespace pcm::spike
