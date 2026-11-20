#include "audio_chunk.h"

#include <cstring>

namespace pcm::webcall {

namespace {
constexpr int kHeaderSize = 16;
constexpr uint8_t kVersion = 1;
} // namespace

std::optional<AudioChunk> parseAudioChunk(const QByteArray &frame) {
  if (frame.size() < kHeaderSize) return std::nullopt;
  const auto *p = reinterpret_cast<const uint8_t *>(frame.constData());
  const int idLen = p[1];
  if (p[0] != kVersion || frame.size() < kHeaderSize + idLen) return std::nullopt;
  const qsizetype pcmBytes = frame.size() - kHeaderSize - idLen;
  if (pcmBytes % 2 != 0) return std::nullopt;

  AudioChunk chunk;
  std::memcpy(&chunk.seq, p + 4, 4);
  std::memcpy(&chunk.sentMs, p + 8, 8);
  chunk.participantId = QString::fromUtf8(frame.constData() + kHeaderSize, idLen);
  chunk.samples = reinterpret_cast<const int16_t *>(p + kHeaderSize + idLen);
  chunk.count = static_cast<std::size_t>(pcmBytes / 2);
  return chunk;
}

bool isSampleAligned(const AudioChunk &chunk) {
  return reinterpret_cast<std::uintptr_t>(chunk.samples) % alignof(int16_t) == 0;
}

} // namespace pcm::webcall
