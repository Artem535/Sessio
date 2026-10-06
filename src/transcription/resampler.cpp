#include "resampler.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

#include "transcription_types.h"

namespace pcm::transcription {

namespace {

// Hamming-windowed sinc low-pass, cut off at 0.45 of the output Nyquist
// frequency's input-rate equivalent, normalised to unit gain at DC.
std::vector<float> makeTaps(int ratio) {
  if (ratio == 1) return {1.0f};
  const int count = 32 * ratio + 1;
  const int middle = (count - 1) / 2;
  const double cutoff = 0.45 / ratio;  // cycles per input sample
  std::vector<double> h(count);
  double sum = 0;
  for (int j = 0; j < count; ++j) {
    const double x = j - middle;
    const double arg = 2.0 * std::numbers::pi * cutoff * x;
    const double sinc = x == 0 ? 1.0 : std::sin(arg) / arg;
    const double window = 0.54 - 0.46 * std::cos(2.0 * std::numbers::pi * j / (count - 1));
    h[j] = 2.0 * cutoff * sinc * window;
    sum += h[j];
  }
  std::vector<float> taps(count);
  for (int j = 0; j < count; ++j) taps[j] = static_cast<float>(h[j] / sum);
  return taps;
}

}  // namespace

Resampler::Resampler(int input_rate) {
  if (input_rate <= 0 || input_rate % kRecognizerSampleRate != 0) {
    throw std::invalid_argument("Resampler: input rate must be a multiple of 16000");
  }
  ratio_ = input_rate / kRecognizerSampleRate;
  taps_ = makeTaps(ratio_);
  history_.assign(taps_.size() - 1, 0.0f);
}

std::vector<float> Resampler::process(std::span<const int16_t> input) {
  const size_t history = taps_.size() - 1;
  std::vector<float> buffer;
  buffer.reserve(history + input.size());
  buffer.insert(buffer.end(), history_.begin(), history_.end());
  for (const int16_t s : input) buffer.push_back(static_cast<float>(s) / 32768.0f);

  std::vector<float> out;
  out.reserve(input.size() / ratio_ + 1);
  for (; next_ < input.size(); next_ += ratio_) {
    // Newest sample for this output is input[next_], at buffer[history + next_].
    float acc = 0.0f;
    const float* newest = buffer.data() + history + next_;
    for (size_t j = 0; j < taps_.size(); ++j) acc += taps_[j] * newest[-static_cast<ptrdiff_t>(j)];
    out.push_back(acc);
  }
  next_ -= input.size();
  history_.assign(buffer.end() - static_cast<ptrdiff_t>(history), buffer.end());
  return out;
}

}  // namespace pcm::transcription
