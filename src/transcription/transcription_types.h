#pragma once

#include <cstdint>
#include <string>

namespace pcm::transcription {

using TrackId = std::string;

enum class TrackRole { Practitioner, Participant };

struct TrackInfo {
  TrackId id;
  TrackRole role = TrackRole::Participant;
  std::string display_name;
};

struct TranscribedPhrase {
  TrackId track_id;
  TrackRole role = TrackRole::Participant;
  std::string speaker_name;
  int64_t start_ms = 0;  // from the start of the call
  int64_t end_ms = 0;
  std::string text;
};

constexpr int kRecognizerSampleRate = 16000;

}  // namespace pcm::transcription
