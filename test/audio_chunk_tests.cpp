#include "audio_chunk.h"

#include <gtest/gtest.h>

#include <cstring>

using pcm::webcall::parseAudioChunk;

namespace {
QByteArray makeFrame(uint8_t version, const QByteArray &id, const QByteArray &pcm, uint32_t seq = 0, double sentMs = 0) {
  QByteArray f;
  f.append(char(version)).append(char(id.size())).append(2, '\0');
  f.append(reinterpret_cast<const char *>(&seq), 4);
  f.append(reinterpret_cast<const char *>(&sentMs), 8);
  f.append(id).append(pcm);
  return f;
}
} // namespace

TEST(AudioChunk, ParsesV1) {
  const int16_t pcm[3] = {1, -2, 300};
  const QByteArray frame = makeFrame(1, "p:Анна", QByteArray(reinterpret_cast<const char *>(pcm), 6), 7, 1234.5);
  const auto chunk = parseAudioChunk(frame);
  ASSERT_TRUE(chunk.has_value());
  EXPECT_EQ(chunk->participantId, QString::fromUtf8("p:Анна"));
  EXPECT_EQ(chunk->seq, 7u);
  EXPECT_DOUBLE_EQ(chunk->sentMs, 1234.5);
  ASSERT_EQ(chunk->count, 3u);
  int16_t got[3];
  std::memcpy(got, chunk->samples, 6);  // may be misaligned
  EXPECT_EQ(got[0], 1);
  EXPECT_EQ(got[1], -2);
  EXPECT_EQ(got[2], 300);
}

TEST(AudioChunk, RejectsWrongVersion) {
  EXPECT_FALSE(parseAudioChunk(makeFrame(2, "a", QByteArray(4, '\0'))).has_value());
}

TEST(AudioChunk, RejectsOddSampleBytes) {
  EXPECT_FALSE(parseAudioChunk(makeFrame(1, "a", QByteArray(3, '\0'))).has_value());
}

TEST(AudioChunk, RejectsShortHeader) {
  EXPECT_FALSE(parseAudioChunk(QByteArray(15, '\0')).has_value());
  // idLen larger than what follows
  QByteArray f = makeFrame(1, "abc", {});
  f.chop(2);
  EXPECT_FALSE(parseAudioChunk(f).has_value());
}
