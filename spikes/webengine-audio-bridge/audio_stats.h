#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <map>
#include <mutex>
#include <vector>

// Chunk wire format shared by every transport (little endian):
//   0 u8 version (1) | 1 u8 idLen | 2 u16 reserved | 4 u32 seq | 8 f64 sentMs
//   16 track id (UTF-8, idLen bytes) | int16 mono PCM samples
// Thread-safe: transports may deliver from any thread.
class AudioStats {
public:
  explicit AudioStats(double warmupMs) : warmupMs_(warmupMs) {}

  // Returns false for a malformed chunk.
  bool onChunk(const QByteArray &blob);
  void countMalformed();

  // produced: chunks the page reports having emitted per track.
  QJsonObject report(const QJsonObject &produced) const;

private:
  struct Track {
    uint32_t lastSeq = 0;
    bool seen = false;
    uint64_t chunks = 0;
    uint64_t samples = 0;
    uint64_t gaps = 0;
    uint64_t reordered = 0;
    double sumSquares = 0;
  };

  mutable std::mutex mutex_;
  const double warmupMs_;
  double firstRecvMs_ = 0;
  uint64_t bytes_ = 0;
  uint64_t messages_ = 0;
  uint64_t malformed_ = 0;
  std::map<QString, Track> tracks_;
  std::vector<double> latencies_;
};

double nowEpochMs();
