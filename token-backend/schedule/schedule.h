#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pcm::schedule {

struct Override {
  int64_t originalStartMs = 0;
  int64_t startMs = 0;
  int64_t endMs = 0;
  bool joinEnabled = false;
};

struct Snapshot {
  int64_t revision = 0;
  int64_t baseRevision = 0;
  std::string timezone;
  std::string dtstartLocal;
  int64_t durationSeconds = 0;
  std::string rrule;
  std::optional<int64_t> untilMs;
  bool active = false;
  bool joinEnabled = false;
  std::vector<Override> overrides;
  std::vector<int64_t> exceptions;
};

struct Occurrence {
  int64_t originalStartMs = 0;
  int64_t startMs = 0;
  int64_t endMs = 0;
  bool joinEnabled = false;
};

enum class Status { Available, Unavailable, Ambiguous, Invalid, BudgetExceeded };

struct Validation {
  bool valid = false;
  std::string error;
};

struct Resolution {
  Status status = Status::Unavailable;
  std::vector<Occurrence> occurrences;
  std::optional<Occurrence> occurrence;
  std::string error;
};

// Directory of libical's builtin timezone files (*.ics per zone). Defaults to
// the path compiled in at build time (backend); the desktop app points it at the
// data it installs. True when the directory contains timezone data. A directory
// without data is still applied: every named zone then fails validation (it
// never silently falls back to something else). Call before first use.
bool setZoneinfoDirectory(const std::string &directory);
std::string zoneinfoDirectory();
// True when `directory` looks like libical timezone data.
bool zoneinfoDirectoryHasData(const std::string &directory);

Validation validate(const Snapshot &snapshot);
// Inclusive range of effective starts. Disabled occurrences remain visible so
// calendar consumers can display canceled slots; inactive series return none.
Resolution occurrencesBetween(const Snapshot &snapshot, int64_t fromMs, int64_t toMs);
// Inclusive join windows; only one enabled occurrence yields occurrence.
Resolution resolve(const Snapshot &snapshot, int64_t nowMs,
                   int64_t prejoinSeconds, int64_t graceSeconds);

} // namespace pcm::schedule
