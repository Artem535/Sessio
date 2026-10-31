#include "audio_stats.h"

#include <QJsonArray>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

double nowEpochMs() {
  using namespace std::chrono;
  return duration<double, std::milli>(system_clock::now().time_since_epoch()).count();
}

bool AudioStats::onChunk(const QByteArray &blob) {
  const double recvMs = nowEpochMs();
  if (blob.size() < 16) {
    countMalformed();
    return false;
  }
  const auto *p = reinterpret_cast<const uint8_t *>(blob.constData());
  const int idLen = p[1];
  if (p[0] != 1 || blob.size() < 16 + idLen || (blob.size() - 16 - idLen) % 2 != 0) {
    countMalformed();
    return false;
  }
  uint32_t seq;
  double sentMs;
  std::memcpy(&seq, p + 4, 4);
  std::memcpy(&sentMs, p + 8, 8);
  const QString id = QString::fromUtf8(blob.constData() + 16, idLen);
  const auto sampleCount = static_cast<std::size_t>((blob.size() - 16 - idLen) / 2);

  double squares = 0;
  for (std::size_t i = 0; i < sampleCount; ++i) {
    int16_t s;
    std::memcpy(&s, p + 16 + idLen + i * 2, 2);
    squares += double(s) * s;
  }

  std::lock_guard lock(mutex_);
  if (firstRecvMs_ == 0) firstRecvMs_ = recvMs;
  bytes_ += blob.size();
  ++messages_;
  Track &t = tracks_[id];
  if (t.seen) {
    if (seq > t.lastSeq + 1) t.gaps += seq - t.lastSeq - 1;
    else if (seq <= t.lastSeq) ++t.reordered;
  }
  if (!t.seen || seq > t.lastSeq) t.lastSeq = seq;
  t.seen = true;
  ++t.chunks;
  t.samples += sampleCount;
  t.sumSquares += squares;
  if (recvMs - firstRecvMs_ >= warmupMs_) latencies_.push_back(recvMs - sentMs);
  return true;
}

void AudioStats::countMalformed() {
  std::lock_guard lock(mutex_);
  ++malformed_;
}

static double percentile(std::vector<double> sorted, double q) {
  if (sorted.empty()) return 0;
  const auto idx = static_cast<std::size_t>(std::clamp(q * (sorted.size() - 1), 0.0, double(sorted.size() - 1)));
  return sorted[idx];
}

QJsonObject AudioStats::report(const QJsonObject &produced) const {
  std::lock_guard lock(mutex_);
  std::vector<double> sorted = latencies_;
  std::sort(sorted.begin(), sorted.end());

  QJsonArray tracks;
  uint64_t lost = 0;
  for (const auto &[id, t] : tracks_) {
    const auto made = static_cast<uint64_t>(produced.value(id).toDouble(-1));
    const uint64_t missing = made >= t.chunks ? made - t.chunks : 0;
    lost += missing;
    tracks.append(QJsonObject{
        {"id", id},
        {"chunks", double(t.chunks)},
        {"produced", produced.contains(id) ? double(made) : -1.0},
        {"missing", double(missing)},
        {"seqGaps", double(t.gaps)},
        {"reordered", double(t.reordered)},
        {"seconds", double(t.samples) / 16000.0},
        {"rms", t.samples ? std::sqrt(t.sumSquares / t.samples) : 0.0},
    });
  }
  return QJsonObject{
      {"messages", double(messages_)},
      {"bytes", double(bytes_)},
      {"malformed", double(malformed_)},
      {"missingChunks", double(lost)},
      {"latencyMs",
       QJsonObject{{"n", double(sorted.size())},
                   {"p50", percentile(sorted, 0.50)},
                   {"p95", percentile(sorted, 0.95)},
                   {"p99", percentile(sorted, 0.99)},
                   {"max", sorted.empty() ? 0.0 : sorted.back()}}},
      {"tracks", tracks},
  };
}
