#pragma once

#include <algorithm>

namespace pcm::transcription {

// User-adjustable speech detection and recogniser settings. Defaults are the
// values measured in the ASR spike; every field is clamped to a safe range.
struct TranscriptionTuning {
  static constexpr float kMinThreshold = 0.2F, kMaxThreshold = 0.9F, kDefaultThreshold = 0.5F;
  static constexpr float kMinSilence = 0.3F, kMaxSilence = 3.0F, kDefaultSilence = 0.8F;
  static constexpr float kMinSpeech = 0.1F, kMaxSpeech = 1.0F, kDefaultSpeech = 0.25F;
  static constexpr float kMinPhrase = 5.0F, kMaxPhrase = 30.0F, kDefaultPhrase = 20.0F;
  static constexpr int kMinThreads = 1, kMaxThreads = 16, kDefaultThreads = 4;

  float threshold = kDefaultThreshold;     // speech probability; higher ignores quieter sounds
  float minSilenceSec = kDefaultSilence;   // pause that ends a phrase
  float minSpeechSec = kDefaultSpeech;     // shorter sounds are dropped
  float maxPhraseSec = kDefaultPhrase;     // longer speech is cut into several phrases
  int numThreads = kDefaultThreads;        // recogniser CPU threads

  [[nodiscard]] TranscriptionTuning clamped() const {
    TranscriptionTuning t = *this;
    t.threshold = std::clamp(t.threshold, kMinThreshold, kMaxThreshold);
    t.minSilenceSec = std::clamp(t.minSilenceSec, kMinSilence, kMaxSilence);
    t.minSpeechSec = std::clamp(t.minSpeechSec, kMinSpeech, kMaxSpeech);
    t.maxPhraseSec = std::clamp(t.maxPhraseSec, kMinPhrase, kMaxPhrase);
    t.numThreads = std::clamp(t.numThreads, kMinThreads, kMaxThreads);
    return t;
  }
  bool operator==(const TranscriptionTuning &) const = default;
};

}  // namespace pcm::transcription
