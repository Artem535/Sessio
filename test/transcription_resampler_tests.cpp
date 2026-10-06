#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

#include "resampler.h"

using pcm::transcription::Resampler;

namespace {

std::vector<int16_t> sine(double hz, int rate, int samples, double amplitude) {
  std::vector<int16_t> out(samples);
  for (int i = 0; i < samples; ++i) {
    out[i] = static_cast<int16_t>(amplitude * std::sin(2.0 * std::numbers::pi * hz * i / rate));
  }
  return out;
}

double rms(const std::vector<float>& v, size_t from) {
  double sum = 0;
  for (size_t i = from; i < v.size(); ++i) sum += static_cast<double>(v[i]) * v[i];
  return std::sqrt(sum / static_cast<double>(v.size() - from));
}

}  // namespace

TEST(ResamplerTest, OutputLengthIsInputOverRatio) {
  Resampler r(48000);
  EXPECT_EQ(r.process(std::vector<int16_t>(480, 0)).size(), 160u);
  EXPECT_EQ(r.process(std::vector<int16_t>(4800, 0)).size(), 1600u);
}

TEST(ResamplerTest, PreservesConstantSignal) {
  Resampler r(48000);
  const auto out = r.process(std::vector<int16_t>(4800, 10000));
  EXPECT_NEAR(out.back(), 10000.0f / 32768.0f, 1e-3);
}

TEST(ResamplerTest, PassesOneKilohertzAndRejectsTwelveKilohertz) {
  Resampler low(48000);
  const auto kept = low.process(sine(1000, 48000, 9600, 10000));
  EXPECT_NEAR(rms(kept, 800), 10000.0 / 32768.0 / std::sqrt(2.0), 0.015);

  Resampler high(48000);
  const auto cut = high.process(sine(12000, 48000, 9600, 10000));
  EXPECT_LT(rms(cut, 800), 0.01 * 10000.0 / 32768.0);
}

TEST(ResamplerTest, ChunkingDoesNotChangeOutput) {
  std::vector<int16_t> input = sine(440, 48000, 4801, 8000);
  Resampler whole(48000);
  const auto expected = whole.process(input);

  Resampler pieces(48000);
  std::vector<float> got;
  for (size_t i = 0; i < input.size(); i += 7) {
    const size_t n = std::min<size_t>(7, input.size() - i);
    const auto part = pieces.process(std::span<const int16_t>(input.data() + i, n));
    got.insert(got.end(), part.begin(), part.end());
  }
  ASSERT_EQ(got.size(), expected.size());
  for (size_t i = 0; i < got.size(); ++i) EXPECT_FLOAT_EQ(got[i], expected[i]);
}

TEST(ResamplerTest, SixteenKilohertzIsPassThrough) {
  Resampler r(16000);
  const auto out = r.process(std::vector<int16_t>{0, 16384, -16384});
  ASSERT_EQ(out.size(), 3u);
  EXPECT_FLOAT_EQ(out[1], 0.5f);
  EXPECT_FLOAT_EQ(out[2], -0.5f);
}

TEST(ResamplerTest, RejectsRatesThatAreNotMultiplesOfSixteenKilohertz) {
  EXPECT_THROW(Resampler(44100), std::invalid_argument);
  EXPECT_THROW(Resampler(0), std::invalid_argument);
}
