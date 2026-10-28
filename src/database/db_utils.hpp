// db_structs.hpp

#pragma once

#include "duckdb.hpp"
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <optional>
#include <string>

namespace pcm::database::db_utils {


inline std::optional<std::string> toOptionalString(const duckdb::Value &val) {
  if (val.IsNull()) return std::nullopt;
  return val.ToString();
}

inline std::optional<std::int64_t> toOptionalInt32AsInt64(const duckdb::Value &val) {
  if (val.IsNull()) return std::nullopt;
  return val.GetValue<int32_t>();
}

inline std::optional<double> toOptionalDouble(const duckdb::Value &val) {
  if (val.IsNull()) return std::nullopt;
  return val.GetValue<double>();
}

inline std::int64_t toInt32AsInt64(const duckdb::Value &val) {
  return val.GetValue<int32_t>();
}

inline bool toBool(const duckdb::Value &val) {
  return val.GetValue<bool>();
}

inline std::optional<std::int64_t> toOptionalTimestampMicros(const duckdb::Value &val) {
  if (val.IsNull()) return std::nullopt;
  auto ts = val.GetValue<duckdb::timestamp_t>();
  return duckdb::Timestamp::GetEpochMicroSeconds(ts);
}

inline std::optional<std::int64_t> toOptionalTimestampMs(const duckdb::Value &val) {
  if (val.IsNull()) return std::nullopt;
  auto ts = val.GetValue<duckdb::timestamp_t>();
  return duckdb::Timestamp::GetEpochMicroSeconds(ts) / 1000;
}

inline duckdb::Value toDuckValue(const std::optional<std::string>& opt) {
  return opt.has_value() ? duckdb::Value(*opt) : duckdb::Value();
}

inline duckdb::Value toDuckValue(const std::optional<std::int64_t>& opt) {
  return opt.has_value() ? duckdb::Value(*opt) : duckdb::Value();
}

inline duckdb::Value toDuckValue(const std::optional<double>& opt) {
  return opt.has_value() ? duckdb::Value::DOUBLE(*opt) : duckdb::Value();
}

inline duckdb::Value toDuckTimestamp(const std::optional<std::int64_t>& micros_opt) {
  if (!micros_opt.has_value()) return duckdb::Value(); // NULL
  auto ts = duckdb::Timestamp::FromEpochMicroSeconds(*micros_opt);
  return duckdb::Value::TIMESTAMP(ts);
}

// Local calendar date ("YYYY-MM-DD") of an epoch-millisecond instant.
inline std::string msToLocalDateString(std::int64_t ms) {
  const std::time_t secs = static_cast<std::time_t>(ms / 1000);
  std::tm tm{};
  localtime_r(&secs, &tm);
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
  return buf;
}

// Local midnight (epoch ms) of a "YYYY-MM-DD" date; 0 when unparsable.
inline std::int64_t localDateStringToMidnightMs(const std::string &date) {
  int y = 0, m = 0, d = 0;
  if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return 0;
  std::tm tm{};
  tm.tm_year = y - 1900;
  tm.tm_mon = m - 1;
  tm.tm_mday = d;
  tm.tm_isdst = -1;
  return static_cast<std::int64_t>(std::mktime(&tm)) * 1000;
}

} // namespace pcm::database::db_utils
