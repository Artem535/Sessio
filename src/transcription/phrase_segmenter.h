#pragma once

#include <memory>
#include <span>
#include <vector>

#include "voice_activity.h"

namespace pcm::transcription {

// Per-track: cuts a 16 kHz stream into fixed windows for the VAD and collects
// the phrases it closes.
class PhraseSegmenter {
 public:
  explicit PhraseSegmenter(std::unique_ptr<IVoiceActivityDetector> vad);

  std::vector<SpeechSegment> feed(std::span<const float> samples);
  // End of stream: pad the partial window with zeros, close an open phrase.
  std::vector<SpeechSegment> flush();

 private:
  std::vector<SpeechSegment> drain();

  std::unique_ptr<IVoiceActivityDetector> vad_;
  std::vector<float> pending_;
};

}  // namespace pcm::transcription
