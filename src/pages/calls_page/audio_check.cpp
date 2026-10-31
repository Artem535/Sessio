#include "audio_check.h"

#include <QPainter>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace pcm::calls {

namespace {
template <typename Sample>
double peakOf(const QByteArray &pcm, double fullScale) {
  const auto count = pcm.size() / static_cast<qsizetype>(sizeof(Sample));
  double peak = 0.0;
  for (qsizetype i = 0; i < count; ++i) {
    Sample sample;
    std::memcpy(&sample, pcm.constData() + i * static_cast<qsizetype>(sizeof(Sample)), sizeof(Sample));
    peak = std::max(peak, std::abs(static_cast<double>(sample)));
  }
  return std::clamp(peak / fullScale, 0.0, 1.0);
}
} // namespace

double normalizedPeak(const QByteArray &pcm, const QAudioFormat::SampleFormat format) {
  switch (format) {
  case QAudioFormat::Int16:
    return peakOf<int16_t>(pcm, 32768.0);
  case QAudioFormat::Int32:
    return peakOf<int32_t>(pcm, 2147483648.0);
  case QAudioFormat::Float:
    return peakOf<float>(pcm, 1.0);
  case QAudioFormat::UInt8: {
    double peak = 0.0;
    for (const char byte : pcm)
      peak = std::max(peak, std::abs(static_cast<double>(static_cast<uint8_t>(byte)) - 128.0));
    return std::clamp(peak / 128.0, 0.0, 1.0);
  }
  default:
    return 0.0;
  }
}

QByteArray makeTestTone(const QAudioFormat &format, const int durationMs, const double frequencyHz) {
  const int channels = std::max(1, format.channelCount());
  const int rate = format.sampleRate();
  if (rate <= 0 || durationMs <= 0) return {};
  const int frames = static_cast<int>(static_cast<qint64>(rate) * durationMs / 1000);
  const int sampleBytes = format.bytesPerSample();
  if (sampleBytes <= 0) return {};
  QByteArray out(static_cast<qsizetype>(frames) * channels * sampleBytes, 0);
  constexpr double kAmplitude = 0.3;
  constexpr double kTwoPi = 6.283185307179586;
  const int fade = std::max(1, rate / 100); // 10 ms ramps so the tone does not click
  for (int frame = 0; frame < frames; ++frame) {
    const double envelope = std::min({1.0, static_cast<double>(frame) / fade,
                                      static_cast<double>(frames - 1 - frame) / fade});
    const double value = kAmplitude * envelope * std::sin(kTwoPi * frequencyHz * frame / rate);
    for (int channel = 0; channel < channels; ++channel) {
      char *dst = out.data() + (static_cast<qsizetype>(frame) * channels + channel) * sampleBytes;
      switch (format.sampleFormat()) {
      case QAudioFormat::Int16: {
        const auto v = static_cast<int16_t>(value * 32767.0);
        std::memcpy(dst, &v, sizeof v);
        break;
      }
      case QAudioFormat::Int32: {
        const auto v = static_cast<int32_t>(value * 2147483647.0);
        std::memcpy(dst, &v, sizeof v);
        break;
      }
      case QAudioFormat::Float: {
        const auto v = static_cast<float>(value);
        std::memcpy(dst, &v, sizeof v);
        break;
      }
      case QAudioFormat::UInt8:
        *reinterpret_cast<uint8_t *>(dst) = static_cast<uint8_t>(128.0 + value * 127.0);
        break;
      default:
        return {};
      }
    }
  }
  return out;
}

MicLevelMeter::MicLevelMeter(QWidget *parent) : QWidget(parent) {
  setObjectName("micLevelMeter");
  setFixedHeight(8);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void MicLevelMeter::setLevel(const double level) {
  const double clamped = std::clamp(level, 0.0, 1.0);
  const double next = clamped >= mLevel ? clamped : mLevel * 0.85 + clamped * 0.15;
  if (std::abs(next - mLevel) < 0.001) return;
  mLevel = next;
  update();
}

void MicLevelMeter::reset() {
  mLevel = 0.0;
  update();
}

int MicLevelMeter::litSegments() const {
  // Perceived loudness is closer to logarithmic: a square root keeps quiet speech visible.
  return static_cast<int>(std::lround(std::sqrt(mLevel) * kSegments));
}

QSize MicLevelMeter::sizeHint() const { return {160, 8}; }

void MicLevelMeter::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);
  const int lit = litSegments();
  const double gap = 3.0;
  const double width = (this->width() - gap * (kSegments - 1)) / kSegments;
  for (int i = 0; i < kSegments; ++i) {
    painter.setBrush(i < lit ? QColor(59, 178, 115) : QColor(255, 255, 255, 40));
    painter.drawRoundedRect(QRectF(i * (width + gap), 0, width, height()), 2.0, 2.0);
  }
}

} // namespace pcm::calls
