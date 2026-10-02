#include "service/schedule_wire.h"
#include "oatpp/parser/json/Utils.hpp"
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <set>
#include <stdexcept>

namespace pcm::tokenbackend {
namespace {
[[noreturn]] void invalid(const char *reason) { throw std::invalid_argument(reason); }
int digits(const std::string &v, size_t from, size_t length) {
  int n = 0;
  for (size_t i = from; i < from + length; ++i) {
    if (v[i] < '0' || v[i] > '9') invalid("invalid_utc_timestamp");
    n = n * 10 + v[i] - '0';
  }
  return n;
}
int64_t utc(const std::string &v) {
  if (v.size() != 20 || v[4] != '-' || v[7] != '-' || v[10] != 'T' ||
      v[13] != ':' || v[16] != ':' || v[19] != 'Z') invalid("invalid_utc_timestamp");
  using namespace std::chrono;
  int y = digits(v, 0, 4), h = digits(v, 11, 2), m = digits(v, 14, 2), s = digits(v, 17, 2);
  year_month_day date{year(y), month(digits(v, 5, 2)), day(digits(v, 8, 2))};
  if (y < 1 || !date.ok() || h > 23 || m > 59 || s > 59) invalid("invalid_utc_timestamp");
  return duration_cast<milliseconds>(sys_days(date).time_since_epoch() + hours(h) + minutes(m) + seconds(s)).count();
}
std::string iso(int64_t ms) {
  using namespace std::chrono;
  auto time = sys_time<milliseconds>(milliseconds(ms));
  auto days = floor<std::chrono::days>(time);
  year_month_day date{days}; hh_mm_ss tod{time - days};
  char out[32];
  std::snprintf(out, sizeof(out), "%04d-%02u-%02uT%02lld:%02lld:%02lldZ", int(date.year()),
                unsigned(date.month()), unsigned(date.day()),
                static_cast<long long>(tod.hours().count()), static_cast<long long>(tod.minutes().count()),
                static_cast<long long>(tod.seconds().count()));
  return out;
}
// Schema-aware reader: only fixed objects and arrays exist in v1. Unknown
// fields fail before descending, bounding nesting and avoiding coercive DTOs.
class Reader {
public:
  explicit Reader(const std::string &json) : json_(json) {}
  bool invitation() {
    bool reissue = false;
    object({"reissue"}, [&](const std::string &) { reissue = boolean(); });
    whitespace(); if (pos_ != json_.size()) invalid("trailing_json");
    return reissue;
  }
  std::string specialistName() {
    std::string name;
    object({"displayName"}, [&](const std::string &) { name = string(); }, false);
    whitespace(); if (pos_ != json_.size()) invalid("trailing_json");
    return name;
  }
  schedule::Snapshot snapshot() {
    schedule::Snapshot s;
    object({"schema_version", "revision", "base_revision", "timezone", "dtstart_local", "duration_seconds", "rrule", "until_utc", "active", "join_enabled", "overrides", "exceptions"}, [&](const std::string &key) {
      if (key == "schema_version") { if (number() != 1) invalid("unsupported_schema_version"); }
      else if (key == "revision") s.revision = number();
      else if (key == "base_revision") s.baseRevision = number();
      else if (key == "timezone") s.timezone = string();
      else if (key == "dtstart_local") s.dtstartLocal = string();
      else if (key == "duration_seconds") s.durationSeconds = number();
      else if (key == "rrule") s.rrule = string();
      else if (key == "until_utc") { if (!literal("null")) s.untilMs = utc(string()); }
      else if (key == "active") s.active = boolean();
      else if (key == "join_enabled") s.joinEnabled = boolean();
      else if (key == "overrides") array([&] { s.overrides.push_back(override()); });
      else if (key == "exceptions") array([&] { s.exceptions.push_back(utc(string())); });
    });
    whitespace(); if (pos_ != json_.size()) invalid("trailing_json");
    return s;
  }
private:
  const std::string &json_;
  size_t pos_ = 0;
  void whitespace() { while (pos_ < json_.size() && (json_[pos_] == ' ' || json_[pos_] == '\t' || json_[pos_] == '\r' || json_[pos_] == '\n')) ++pos_; }
  bool take(char c) { whitespace(); if (pos_ < json_.size() && json_[pos_] == c) { ++pos_; return true; } return false; }
  void require(char c) { if (!take(c)) invalid("malformed_json"); }
  bool literal(const std::string &value) {
    whitespace(); if (json_.compare(pos_, value.size(), value) != 0) return false;
    pos_ += value.size(); return true;
  }
  bool boolean() { if (literal("true")) return true; if (literal("false")) return false; invalid("boolean_required"); }
  std::string string() {
    whitespace(); if (pos_ == json_.size() || json_[pos_] != '"') invalid("string_required");
    oatpp::parser::Caret caret(json_.data() + pos_, json_.size() - pos_);
    auto value = oatpp::parser::json::Utils::parseStringToStdString(caret);
    if (caret.hasError()) invalid("malformed_json_string");
    pos_ += caret.getPosition();
    for (unsigned char c : value) if (c < 32 || c == 127) invalid("invalid_string_character");
    return value;
  }
  int64_t number() {
    whitespace(); size_t begin = pos_;
    if (pos_ < json_.size() && json_[pos_] == '-') ++pos_;
    size_t first = pos_;
    while (pos_ < json_.size() && json_[pos_] >= '0' && json_[pos_] <= '9') ++pos_;
    if (first == pos_ || (json_[first] == '0' && pos_ > first + 1)) invalid("integer_required");
    int64_t n;
    auto parsed = std::from_chars(json_.data() + begin, json_.data() + pos_, n);
    if (parsed.ec != std::errc{}) invalid("integer_out_of_range");
    return n;
  }
  template<class F> void object(const std::set<std::string> &allowed, F read, bool requireAll = true) {
    require('{'); std::set<std::string> seen;
    if (!take('}')) {
      do {
        auto key = string();
        if (!allowed.contains(key)) invalid("unknown_field");
        if (!seen.insert(key).second) invalid("duplicate_field");
        require(':'); read(key);
        if (take('}')) break;
        require(',');
      } while (true);
    }
    if (requireAll && seen != allowed) invalid("missing_field");
  }
  template<class F> void array(F read) {
    require('['); size_t count = 0;
    if (take(']')) return;
    do {
      if (++count > 1000) invalid("too_many_entries");
      read(); if (take(']')) break; require(',');
    } while (true);
  }
  schedule::Override override() {
    schedule::Override o;
    object({"original_start_utc", "start_utc", "end_utc", "join_enabled"}, [&](const std::string &key) {
      if (key == "original_start_utc") o.originalStartMs = utc(string());
      else if (key == "start_utc") o.startMs = utc(string());
      else if (key == "end_utc") o.endMs = utc(string());
      else o.joinEnabled = boolean();
    });
    return o;
  }
};
} // namespace

schedule::Snapshot parseScheduleJson(const std::string &json) { return Reader(json).snapshot(); }
bool parseSeriesInvitationJson(const std::string &json) { return Reader(json).invitation(); }
std::string parseSpecialistNameJson(const std::string &json) { return Reader(json).specialistName(); }
int64_t parseUtcTimestamp(const std::string &value) { return utc(value); }
std::string utcTimestamp(int64_t ms) { return iso(ms); }
std::string jsonQuote(const std::string &value) {
  return "\"" + *oatpp::parser::json::Utils::escapeString(value.data(), value.size()) + "\"";
}
std::string scheduleJson(const schedule::Snapshot &s) {
  std::string out = "{\"schema_version\":1,\"revision\":" + std::to_string(s.revision) +
      ",\"base_revision\":" + std::to_string(s.baseRevision) + ",\"timezone\":" + jsonQuote(s.timezone) +
      ",\"dtstart_local\":" + jsonQuote(s.dtstartLocal) + ",\"duration_seconds\":" + std::to_string(s.durationSeconds) +
      ",\"rrule\":" + jsonQuote(s.rrule) + ",\"until_utc\":" + (s.untilMs ? jsonQuote(iso(*s.untilMs)) : "null") +
      ",\"active\":" + (s.active ? "true" : "false") + ",\"join_enabled\":" + (s.joinEnabled ? "true" : "false") + ",\"overrides\":[";
  bool first = true;
  for (const auto &o : s.overrides) {
    if (!first) out += ','; first = false;
    out += "{\"original_start_utc\":" + jsonQuote(iso(o.originalStartMs)) + ",\"start_utc\":" + jsonQuote(iso(o.startMs)) +
        ",\"end_utc\":" + jsonQuote(iso(o.endMs)) + ",\"join_enabled\":" + (o.joinEnabled ? "true" : "false") + "}";
  }
  out += "],\"exceptions\":["; first = true;
  for (auto e : s.exceptions) { if (!first) out += ','; first = false; out += jsonQuote(iso(e)); }
  return out + "]}";
}
} // namespace pcm::tokenbackend
