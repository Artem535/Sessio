#pragma once

#include <span>
#include <string>

namespace pcm::transcription {

// Turns one finished phrase (16 kHz float mono) into text. Throws
// std::runtime_error when decoding fails. Called from the decode worker only.
class ISpeechRecognizer {
 public:
  virtual ~ISpeechRecognizer() = default;
  virtual std::string transcribe(std::span<const float> samples) = 0;
};

}  // namespace pcm::transcription
