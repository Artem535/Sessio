#include "phrase_segmenter.h"

#include <cstddef>
#include <utility>

namespace pcm::transcription {

PhraseSegmenter::PhraseSegmenter(std::unique_ptr<IVoiceActivityDetector> vad)
    : vad_(std::move(vad)) {}

std::vector<SpeechSegment> PhraseSegmenter::feed(std::span<const float> samples) {
  pending_.insert(pending_.end(), samples.begin(), samples.end());
  const size_t window = vad_->windowSize();
  size_t offset = 0;
  while (pending_.size() - offset >= window) {
    vad_->accept(pending_.data() + offset, window);
    offset += window;
  }
  pending_.erase(pending_.begin(), pending_.begin() + static_cast<ptrdiff_t>(offset));
  return drain();
}

std::vector<SpeechSegment> PhraseSegmenter::flush() {
  if (!pending_.empty()) {
    pending_.resize(vad_->windowSize(), 0.0f);
    vad_->accept(pending_.data(), pending_.size());
    pending_.clear();
  }
  vad_->flush();
  return drain();
}

std::vector<SpeechSegment> PhraseSegmenter::drain() {
  std::vector<SpeechSegment> out;
  while (vad_->hasSegment()) out.push_back(vad_->popSegment());
  return out;
}

}  // namespace pcm::transcription
