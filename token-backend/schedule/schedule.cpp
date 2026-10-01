#include "schedule.h"

#include <libical/ical.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <set>

namespace {
using namespace pcm::schedule;
using namespace std::chrono;
constexpr int64_t dayMs = 86400000;
constexpr int64_t minMs = -62135596800000; // 0001-01-01
constexpr int64_t maxMs = 253402300799000; // 9999-12-31T23:59:59
std::timed_mutex icalMutex; // libical builtin timezone caches are process global.

void initializeTimezoneData() {
  static const bool initialized = [] {
    icaltimezone_set_builtin_tzdata(1);
    set_zone_directory(PCM_SCHEDULE_ZONEINFO_DIR);
    return true;
  }();
  (void)initialized;
}

bool instant(int64_t value) { return value >= minMs && value <= maxMs && value % 1000 == 0; }

bool number(const std::string &value, int &out) {
  const auto result = std::from_chars(value.data(), value.data() + value.size(), out);
  return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}

std::optional<icaltimetype> localTime(const std::string &s) {
  if (s.size() != 19 || s[4] != '-' || s[7] != '-' || s[10] != 'T' ||
      s[13] != ':' || s[16] != ':') return {};
  for (size_t i = 0; i < s.size(); ++i) {
    if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) continue;
    if (s[i] < '0' || s[i] > '9') return {};
  }
  auto t = icaltime_null_time();
  if (!number(s.substr(0, 4), t.year) || !number(s.substr(5, 2), t.month) ||
      !number(s.substr(8, 2), t.day) || !number(s.substr(11, 2), t.hour) ||
      !number(s.substr(14, 2), t.minute) || !number(s.substr(17, 2), t.second)) return {};
  const year_month_day date{year{t.year}, month{static_cast<unsigned>(t.month)},
      day{static_cast<unsigned>(t.day)}};
  if (t.year < 1 || t.year > 9999 || !date.ok() || t.hour < 0 || t.hour > 23 ||
      t.minute < 0 || t.minute > 59 || t.second < 0 || t.second > 59) return {};
  return t;
}

int64_t naiveMs(icaltimetype t) {
  return duration_cast<milliseconds>((sys_days{year{t.year}/month{static_cast<unsigned>(t.month)}/
      day{static_cast<unsigned>(t.day)}} + hours{t.hour} + minutes{t.minute} + seconds{t.second})
      .time_since_epoch()).count();
}

struct Parsed {
  icaltimetype start = icaltime_null_time();
  icaltimezone *zone = nullptr;
  std::string rule;
  std::optional<int64_t> until;
};

Validation parse(const Snapshot &s, Parsed &p) {
  initializeTimezoneData();
  if (s.baseRevision < 0 || s.baseRevision == INT64_MAX || s.revision != s.baseRevision + 1)
    return {false, "invalid revision"};
  if (s.durationSeconds < 1 || s.durationSeconds > 86400 || s.rrule.empty() || s.rrule.size() > 512 ||
      s.overrides.size() > 1000 || s.exceptions.size() > 1000)
    return {false, "schedule limits exceeded"};
  const auto start = localTime(s.dtstartLocal);
  if (!start) return {false, "invalid dtstart_local"};
  if (s.timezone.empty() || s.timezone.size() > 255 || s.timezone.find('\0') != std::string::npos)
    return {false, "invalid timezone"};
  p.zone = s.timezone == "UTC" ? icaltimezone_get_utc_timezone() :
      icaltimezone_get_builtin_timezone(s.timezone.c_str());
  if (!p.zone) return {false, "unknown timezone"};
  p.start = *start;
  p.until = s.untilMs;
  if (p.until && !instant(*p.until)) return {false, "invalid until"};
  std::map<std::string, std::string> fields;
  size_t pos = 0;
  while (pos < s.rrule.size()) {
    const auto end = s.rrule.find(';', pos);
    const auto part = s.rrule.substr(pos, end == std::string::npos ? end : end - pos);
    const auto eq = part.find('=');
    if (eq == std::string::npos || eq == 0 || eq + 1 == part.size() ||
        !fields.emplace(part.substr(0, eq), part.substr(eq + 1)).second)
      return {false, "invalid RRULE field"};
    if (end == std::string::npos) break;
    pos = end + 1;
    if (pos == s.rrule.size()) return {false, "invalid RRULE separator"};
  }
  const auto freq = fields["FREQ"];
  if (freq != "DAILY" && freq != "WEEKLY" && freq != "MONTHLY" && freq != "YEARLY")
    return {false, "unsupported frequency"};
  for (const auto &[key, value] : fields) {
    if (key == "FREQ") continue;
    if (key == "INTERVAL") {
      int interval = 0;
      if (!number(value, interval) || interval < 1 || interval > 52)
        return {false, "unsupported interval"};
    } else if (key == "BYDAY" && freq == "WEEKLY") {
      std::set<std::string> days;
      size_t begin = 0;
      while (begin < value.size()) {
        const auto token = value.substr(begin, 2);
        if ((token != "MO" && token != "TU" && token != "WE" && token != "TH" &&
             token != "FR" && token != "SA" && token != "SU") || !days.insert(token).second)
          return {false, "unsupported BYDAY"};
        begin += 2;
        if (begin == value.size()) break;
        if (value[begin] != ',' || ++begin == value.size()) return {false, "invalid BYDAY"};
      }
    } else if (key == "BYMONTHDAY" && (freq == "MONTHLY" || freq == "YEARLY")) {
      int day = 0;
      if (!number(value, day) || day != p.start.day) return {false, "unsupported BYMONTHDAY"};
    } else if (key == "BYMONTH" && freq == "YEARLY") {
      int month = 0;
      if (!number(value, month) || month != p.start.month) return {false, "unsupported BYMONTH"};
    } else if (key == "UNTIL") {
      if (value.size() != 16 || value[8] != 'T' || value[15] != 'Z')
        return {false, "invalid RRULE UNTIL"};
      const auto until = localTime(value.substr(0, 4) + "-" + value.substr(4, 2) + "-" +
          value.substr(6, 2) + "T" + value.substr(9, 2) + ":" + value.substr(11, 2) + ":" +
          value.substr(13, 2));
      if (!until) return {false, "invalid RRULE UNTIL"};
      const auto ms = naiveMs(*until);
      p.until = p.until ? std::min(*p.until, ms) : ms;
    } else return {false, "unsupported RRULE field"};
  }
  // DTSTART supplies omitted selectors; reject richer forms rather than guessing.
  for (const auto &[key, value] : fields) {
    if (key != "UNTIL") p.rule += (p.rule.empty() ? "" : ";") + key + "=" + value;
  }
  std::set<int64_t> keys;
  for (const auto &o : s.overrides) {
    if (!instant(o.originalStartMs) || !instant(o.startMs) || !instant(o.endMs) ||
        o.endMs <= o.startMs || !keys.insert(o.originalStartMs).second)
      return {false, "invalid or duplicate override"};
  }
  keys.clear();
  for (const auto key : s.exceptions) {
    if (!instant(key) || !keys.insert(key).second) return {false, "invalid or duplicate exception"};
  }
  return {true, {}};
}

// Test UTC candidates against the local wall clock. No match is a DST gap;
// multiple matches are a fold, where the earlier UTC instant is authoritative.
std::optional<int64_t> toUtc(icaltimetype local, icaltimezone *zone) {
  const auto naive = naiveMs(local);
  std::set<int> offsets;
  for (int days = -2; days <= 2; ++days) {
    auto sample = icaltime_from_timet_with_zone((naive + days * dayMs) / 1000, 0,
        icaltimezone_get_utc_timezone());
    int daylight = 0;
    offsets.insert(icaltimezone_get_utc_offset_of_utc_time(zone, &sample, &daylight));
  }
  std::optional<int64_t> result;
  for (const int offset : offsets) {
    const auto candidate = naive - static_cast<int64_t>(offset) * 1000;
    const auto roundtrip = icaltime_from_timet_with_zone(candidate / 1000, 0, zone);
    if (naiveMs(roundtrip) == naive && (!result || candidate < *result)) result = candidate;
  }
  return result;
}

Resolution failure(Status status, const std::string &error) {
  Resolution r;
  r.status = status;
  r.error = error;
  return r;
}

Resolution between(const Snapshot &s, int64_t from, int64_t to,
                   steady_clock::time_point deadline) {
  Parsed parsed;
  const auto validation = parse(s, parsed);
  if (!validation.valid) return failure(Status::Invalid, validation.error);
  if (from < minMs || to > maxMs || from > to) return failure(Status::Invalid, "invalid range");
  Resolution result;
  if (!s.active) return result;
  std::set<int64_t> hidden(s.exceptions.begin(), s.exceptions.end());
  for (const auto &o : s.overrides) {
    hidden.insert(o.originalStartMs);
    if (o.startMs >= from && o.startMs <= to)
      result.occurrences.push_back({o.originalStartMs, o.startMs, o.endMs, o.joinEnabled});
  }
  auto rule = icalrecurrencetype_from_string(parsed.rule.c_str());
  std::unique_ptr<icalrecur_iterator, decltype(&icalrecur_iterator_free)> iterator(
      icalrecur_iterator_new(rule, parsed.start), icalrecur_iterator_free);
  std::free(rule.rscale);
  if (!iterator) return failure(Status::Invalid, "invalid recurrence");
  // Seek directly near the requested range, preserving the original interval
  // phase. Padding covers timezone transitions without replaying series history.
  const auto seekMs = std::max(minMs, from - 2 * dayMs);
  auto seek = icaltime_from_timet_with_zone(seekMs / 1000, 0, parsed.zone);
  seek.zone = nullptr;
  if (icaltime_compare(seek, parsed.start) > 0 && !icalrecur_iterator_set_start(iterator.get(), seek))
    return failure(Status::Invalid, "recurrence seek failed");
  bool finished = false;
  for (int i = 0; i < 10000; ++i) {
    if (steady_clock::now() >= deadline) return failure(Status::BudgetExceeded, "resolution deadline exceeded");
    const auto next = icalrecur_iterator_next(iterator.get());
    if (icaltime_is_null_time(next) || next.year > 9999 || naiveMs(next) > to + 2 * dayMs) {
      finished = true;
      break;
    }
    const auto start = toUtc(next, parsed.zone);
    if (!start) continue;
    if (parsed.until && *start > *parsed.until) { finished = true; break; }
    if (*start < from || *start > to || hidden.contains(*start)) continue;
    const auto end = *start + s.durationSeconds * 1000;
    if (end > maxMs) return failure(Status::Invalid, "occurrence end out of range");
    result.occurrences.push_back({*start, *start, end, s.joinEnabled});
  }
  if (!finished) return failure(Status::BudgetExceeded, "resolution iteration budget exceeded");
  std::sort(result.occurrences.begin(), result.occurrences.end(), [](const auto &a, const auto &b) {
    return a.startMs != b.startMs ? a.startMs < b.startMs : a.originalStartMs < b.originalStartMs;
  });
  result.status = result.occurrences.empty() ? Status::Unavailable : Status::Available;
  return result;
}
}

namespace pcm::schedule {
Validation validate(const Snapshot &s) {
  std::lock_guard lock(icalMutex);
  Parsed parsed;
  return parse(s, parsed);
}
Resolution occurrencesBetween(const Snapshot &s, int64_t from, int64_t to) {
  const auto deadline = steady_clock::now() + milliseconds{100};
  std::unique_lock lock(icalMutex, std::defer_lock);
  if (!lock.try_lock_until(deadline)) return failure(Status::BudgetExceeded, "resolution lock deadline exceeded");
  auto result = between(s, from, to, deadline);
  if (steady_clock::now() >= deadline)
    return failure(Status::BudgetExceeded, "resolution deadline exceeded");
  return result;
}
Resolution resolve(const Snapshot &s, int64_t now, int64_t prejoinSeconds, int64_t graceSeconds) {
  constexpr int64_t maxBufferSeconds = (maxMs - minMs) / 1000;
  if (now < minMs || now > maxMs || prejoinSeconds < 0 || graceSeconds < 0 ||
      prejoinSeconds > maxBufferSeconds || graceSeconds > maxBufferSeconds)
    return failure(Status::Invalid, "invalid join window");
  const auto deadline = steady_clock::now() + milliseconds{100};
  std::unique_lock lock(icalMutex, std::defer_lock);
  if (!lock.try_lock_until(deadline)) return failure(Status::BudgetExceeded, "resolution lock deadline exceeded");
  const auto prejoin = prejoinSeconds * 1000;
  const auto grace = graceSeconds * 1000;
  // Use the maximum supported base duration before validation, so malformed
  // caller integers never enter unchecked interval arithmetic.
  const auto from = std::max(minMs, now - dayMs - grace);
  const auto to = std::min(maxMs, now + prejoin);
  auto result = between(s, from, to, deadline);
  if (result.status == Status::Invalid || result.status == Status::BudgetExceeded) return result;
  if (!s.active) return result;
  for (const auto &o : s.overrides) {
    if (o.startMs < from && o.joinEnabled && now >= o.startMs - prejoin && now <= o.endMs + grace)
      result.occurrences.push_back({o.originalStartMs, o.startMs, o.endMs, o.joinEnabled});
  }
  std::erase_if(result.occurrences, [&](const auto &o) {
    return !o.joinEnabled || now < o.startMs - prejoin || now > o.endMs + grace;
  });
  if (steady_clock::now() >= deadline) return failure(Status::BudgetExceeded, "resolution deadline exceeded");
  result.status = result.occurrences.empty() ? Status::Unavailable :
      result.occurrences.size() == 1 ? Status::Available : Status::Ambiguous;
  if (result.status == Status::Available) result.occurrence = result.occurrences.front();
  return result;
}
}
