#include "audio_check.h"

#include <QApplication>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>

using namespace pcm::calls;

namespace {
QAudioFormat format(QAudioFormat::SampleFormat sampleFormat, int channels = 1, int rate = 48000) {
  QAudioFormat f;
  f.setSampleRate(rate);
  f.setChannelCount(channels);
  f.setSampleFormat(sampleFormat);
  return f;
}
} // namespace

TEST(NormalizedPeakTest, SilenceIsZeroAndFullScaleIsOne) {
  const int16_t samples[] = {0, 0, 0};
  EXPECT_DOUBLE_EQ(normalizedPeak(QByteArray(reinterpret_cast<const char *>(samples), sizeof samples),
                                  QAudioFormat::Int16), 0.0);
  const int16_t loud[] = {100, -32768, 5};
  EXPECT_DOUBLE_EQ(normalizedPeak(QByteArray(reinterpret_cast<const char *>(loud), sizeof loud),
                                  QAudioFormat::Int16), 1.0);
}

TEST(NormalizedPeakTest, UsesTheLoudestSampleOfAFloatFormat) {
  const float samples[] = {0.1f, -0.5f, 0.25f};
  EXPECT_NEAR(normalizedPeak(QByteArray(reinterpret_cast<const char *>(samples), sizeof samples),
                             QAudioFormat::Float), 0.5, 1e-6);
}

TEST(NormalizedPeakTest, UnknownFormatReadsAsSilence) {
  EXPECT_DOUBLE_EQ(normalizedPeak(QByteArray(8, '\x7f'), QAudioFormat::Unknown), 0.0);
}

TEST(MakeTestToneTest, HasTheRequestedLengthAndIsAudibleButNotFullScale) {
  const auto f = format(QAudioFormat::Int16, /*channels=*/2);
  const auto tone = makeTestTone(f, 500, 440.0);
  EXPECT_EQ(tone.size(), 24000 * 2 * 2); // 0.5 s * 48000 frames * 2 channels * 2 bytes
  const double peak = normalizedPeak(tone, QAudioFormat::Int16);
  EXPECT_GT(peak, 0.1);
  EXPECT_LT(peak, 0.5);
}

TEST(MakeTestToneTest, FadesInAndOutSoItDoesNotClick) {
  const auto tone = makeTestTone(format(QAudioFormat::Int16), 300, 440.0);
  int16_t first, last;
  std::memcpy(&first, tone.constData(), sizeof first);
  std::memcpy(&last, tone.constData() + tone.size() - sizeof last, sizeof last);
  EXPECT_EQ(first, 0);
  EXPECT_LT(std::abs(last), 200);
}

TEST(MakeTestToneTest, FloatFormatProducesFloatSamples) {
  const auto tone = makeTestTone(format(QAudioFormat::Float), 100, 440.0);
  EXPECT_EQ(tone.size(), 4800 * 4);
  EXPECT_GT(normalizedPeak(tone, QAudioFormat::Float), 0.1);
}

TEST(MakeTestToneTest, UnsupportedFormatYieldsNothing) {
  EXPECT_TRUE(makeTestTone(format(QAudioFormat::Unknown), 100, 440.0).isEmpty());
}

TEST(MicLevelMeterTest, RisesAtOnceFallsGraduallyAndResets) {
  MicLevelMeter meter;
  EXPECT_EQ(meter.litSegments(), 0);
  meter.setLevel(0.64); // sqrt = 0.8 -> 10 of 12 segments
  EXPECT_EQ(meter.litSegments(), 10);
  meter.setLevel(0.0);
  EXPECT_GT(meter.level(), 0.3);        // decays, does not drop to zero in one step
  EXPECT_LT(meter.level(), 0.64);
  meter.reset();
  EXPECT_EQ(meter.litSegments(), 0);
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
