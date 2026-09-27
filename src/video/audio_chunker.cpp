#include "audio_chunker.h"

namespace pcm::video {

AudioChunker::AudioChunker(const std::size_t samplesPerChannel, const int channels)
    : mFrameSize(samplesPerChannel * static_cast<std::size_t>(channels)) {}

std::vector<std::vector<int16_t>>
AudioChunker::push(const std::vector<int16_t> &newSamples) {
  mBuffer.insert(mBuffer.end(), newSamples.begin(), newSamples.end());

  std::vector<std::vector<int16_t>> frames;
  while (mBuffer.size() >= mFrameSize) {
    frames.emplace_back(mBuffer.begin(), mBuffer.begin() + static_cast<long>(mFrameSize));
    mBuffer.erase(mBuffer.begin(), mBuffer.begin() + static_cast<long>(mFrameSize));
  }
  return frames;
}

void AudioChunker::reset() {
  mBuffer.clear();
}

} // namespace pcm::video
