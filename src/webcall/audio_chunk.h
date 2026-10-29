#pragma once

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace pcm::webcall {

// One 20 ms slice of a participant's 16 kHz mono PCM, as sent by the page.
// `samples` points into the frame buffer given to parseAudioChunk(): it is only
// valid while that buffer lives (for the bridge: during the sink call), and it
// may be misaligned because the participant id has an arbitrary length. Check
// isSampleAligned() before dereferencing, otherwise copy with memcpy.
struct AudioChunk {
  QString participantId;
  uint32_t seq = 0;
  double sentMs = 0;
  const int16_t *samples = nullptr;
  std::size_t count = 0;
};

// Wire format, little endian:
//   u8 version=1 | u8 idLen | u16 reserved | u32 seq | f64 sentMs |
//   id (UTF-8, idLen bytes) | int16 mono PCM
// Returns nullopt for a wrong version, a short frame or an odd PCM byte count.
std::optional<AudioChunk> parseAudioChunk(const QByteArray &frame);

bool isSampleAligned(const AudioChunk &chunk);

} // namespace pcm::webcall
