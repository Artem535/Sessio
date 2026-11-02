#include "schedule_snapshot.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace pcm::meeting {
namespace {

constexpr auto kUtcFormat = "yyyy-MM-dd'T'HH:mm:ss'Z'";
constexpr auto kLocalFormat = "yyyy-MM-dd'T'HH:mm:ss";

std::int64_t floorToSecondMs(const std::int64_t ms) {
  return (ms >= 0 ? ms : ms - 999) / 1000 * 1000;
}

QString utcString(const std::int64_t ms) {
  return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC).toString(QLatin1String(kUtcFormat));
}

std::optional<std::int64_t> parseUtc(const QJsonValue &value) {
  if (!value.isString()) {
    return std::nullopt;
  }
  const auto text = value.toString();
  if (text.size() != 20) {
    return std::nullopt;
  }
  auto parsed = QDateTime::fromString(text, QLatin1String(kUtcFormat));
  if (!parsed.isValid()) {
    return std::nullopt;
  }
  parsed.setTimeZone(QTimeZone::UTC);
  return parsed.toMSecsSinceEpoch();
}

std::optional<std::int64_t> parseInteger(const QJsonValue &value) {
  if (!value.isDouble()) {
    return std::nullopt;
  }
  const double number = value.toDouble();
  if (std::floor(number) != number || std::fabs(number) > 9007199254740992.0) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(number);
}

} // namespace

bool statusDisablesJoin(const std::int64_t eventStatusId) {
  return eventStatusId == 3 || eventStatusId == 5 || eventStatusId == 6;
}

std::optional<pcm::schedule::Snapshot>
buildScheduleSnapshot(const pcm::database::ScheduleSource &source) {
  const auto &series = source.series;
  if (source.identity.timezone.empty() || !series.start_date.has_value() ||
      !series.end_date.has_value() || *series.end_date <= *series.start_date) {
    return std::nullopt;
  }
  const QTimeZone zone(QByteArray::fromStdString(source.identity.timezone));
  if (!zone.isValid()) {
    return std::nullopt;
  }

  pcm::schedule::Snapshot snapshot;
  snapshot.timezone = source.identity.timezone;
  snapshot.dtstartLocal = QDateTime::fromMSecsSinceEpoch(floorToSecondMs(*series.start_date), zone)
                              .toString(QLatin1String(kLocalFormat))
                              .toStdString();
  snapshot.durationSeconds = (*series.end_date - *series.start_date) / 1000;
  snapshot.rrule = series.recurrence_rule;
  if (series.recurrence_until.has_value()) {
    snapshot.untilMs = floorToSecondMs(*series.recurrence_until);
  }
  snapshot.active = series.active;
  snapshot.joinEnabled = !statusDisablesJoin(series.event_stat_id);

  for (const auto &item : source.overrides) {
    snapshot.overrides.push_back({floorToSecondMs(item.original_start_ms),
                                  floorToSecondMs(item.start_ms), floorToSecondMs(item.end_ms),
                                  snapshot.joinEnabled && !statusDisablesJoin(item.event_stat_id)});
  }
  std::sort(snapshot.overrides.begin(), snapshot.overrides.end(),
            [](const auto &a, const auto &b) { return a.originalStartMs < b.originalStartMs; });

  for (const auto exception : source.exceptions) {
    snapshot.exceptions.push_back(floorToSecondMs(exception));
  }
  std::sort(snapshot.exceptions.begin(), snapshot.exceptions.end());
  snapshot.exceptions.erase(std::unique(snapshot.exceptions.begin(), snapshot.exceptions.end()),
                            snapshot.exceptions.end());
  return snapshot;
}

std::optional<std::string>
buildScheduleSnapshotPayload(const pcm::database::ScheduleSource &source) {
  const auto snapshot = buildScheduleSnapshot(source);
  if (!snapshot.has_value()) {
    return std::nullopt;
  }
  return serializeSnapshot(*snapshot).toStdString();
}

QByteArray serializeSnapshot(const pcm::schedule::Snapshot &snapshot) {
  QJsonArray overrides;
  for (const auto &item : snapshot.overrides) {
    overrides.append(QJsonObject{{"original_start_utc", utcString(item.originalStartMs)},
                                 {"start_utc", utcString(item.startMs)},
                                 {"end_utc", utcString(item.endMs)},
                                 {"join_enabled", item.joinEnabled}});
  }
  QJsonArray exceptions;
  for (const auto exception : snapshot.exceptions) {
    exceptions.append(utcString(exception));
  }
  QJsonObject object{
      {"schema_version", 1},
      {"revision", static_cast<qint64>(snapshot.revision)},
      {"base_revision", static_cast<qint64>(snapshot.baseRevision)},
      {"timezone", QString::fromStdString(snapshot.timezone)},
      {"dtstart_local", QString::fromStdString(snapshot.dtstartLocal)},
      {"duration_seconds", static_cast<qint64>(snapshot.durationSeconds)},
      {"rrule", QString::fromStdString(snapshot.rrule)},
      {"until_utc", snapshot.untilMs.has_value() ? QJsonValue(utcString(*snapshot.untilMs))
                                                 : QJsonValue(QJsonValue::Null)},
      {"active", snapshot.active},
      {"join_enabled", snapshot.joinEnabled},
      {"overrides", overrides},
      {"exceptions", exceptions}};
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<pcm::schedule::Snapshot> parseSnapshot(const QByteArray &json) {
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(json, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return std::nullopt;
  }
  const auto object = document.object();
  for (const char *key : {"schema_version", "revision", "base_revision", "timezone",
                          "dtstart_local", "duration_seconds", "rrule", "until_utc", "active",
                          "join_enabled", "overrides", "exceptions"}) {
    if (!object.contains(QLatin1String(key))) {
      return std::nullopt;
    }
  }
  if (parseInteger(object["schema_version"]) != 1) {
    return std::nullopt;
  }

  pcm::schedule::Snapshot snapshot;
  const auto revision = parseInteger(object["revision"]);
  const auto base = parseInteger(object["base_revision"]);
  const auto duration = parseInteger(object["duration_seconds"]);
  if (!revision || !base || !duration || !object["timezone"].isString() ||
      !object["dtstart_local"].isString() || !object["rrule"].isString() ||
      !object["active"].isBool() || !object["join_enabled"].isBool() ||
      !object["overrides"].isArray() || !object["exceptions"].isArray()) {
    return std::nullopt;
  }
  snapshot.revision = *revision;
  snapshot.baseRevision = *base;
  snapshot.durationSeconds = *duration;
  snapshot.timezone = object["timezone"].toString().toStdString();
  snapshot.dtstartLocal = object["dtstart_local"].toString().toStdString();
  snapshot.rrule = object["rrule"].toString().toStdString();
  snapshot.active = object["active"].toBool();
  snapshot.joinEnabled = object["join_enabled"].toBool();
  if (!object["until_utc"].isNull()) {
    const auto until = parseUtc(object["until_utc"]);
    if (!until) {
      return std::nullopt;
    }
    snapshot.untilMs = *until;
  }
  for (const auto &entry : object["overrides"].toArray()) {
    const auto item = entry.toObject();
    const auto original = parseUtc(item["original_start_utc"]);
    const auto start = parseUtc(item["start_utc"]);
    const auto end = parseUtc(item["end_utc"]);
    if (!original || !start || !end || !item["join_enabled"].isBool()) {
      return std::nullopt;
    }
    snapshot.overrides.push_back({*original, *start, *end, item["join_enabled"].toBool()});
  }
  for (const auto &entry : object["exceptions"].toArray()) {
    const auto exception = parseUtc(entry);
    if (!exception) {
      return std::nullopt;
    }
    snapshot.exceptions.push_back(*exception);
  }
  return snapshot;
}

std::string scheduleContentHash(const pcm::schedule::Snapshot &snapshot) {
  auto overrides = snapshot.overrides;
  std::sort(overrides.begin(), overrides.end(),
            [](const auto &a, const auto &b) { return a.originalStartMs < b.originalStartMs; });
  auto exceptions = snapshot.exceptions;
  std::sort(exceptions.begin(), exceptions.end());

  // Mirrors token-backend contentHash(): length-prefixed fields are unambiguous.
  std::string canonical = "pcm-schedule-v1:";
  const auto field = [&canonical](const std::string &value) {
    canonical += std::to_string(value.size()) + ":" + value;
  };
  const auto flag = [](const bool value) { return std::string(value ? "true" : "false"); };
  field(snapshot.timezone);
  field(snapshot.dtstartLocal);
  field(std::to_string(snapshot.durationSeconds));
  field(snapshot.rrule);
  field(snapshot.untilMs ? std::to_string(*snapshot.untilMs) : "null");
  field(flag(snapshot.active));
  field(flag(snapshot.joinEnabled));
  field(std::to_string(overrides.size()));
  for (const auto &item : overrides) {
    field(std::to_string(item.originalStartMs));
    field(std::to_string(item.startMs));
    field(std::to_string(item.endMs));
    field(flag(item.joinEnabled));
  }
  field(std::to_string(exceptions.size()));
  for (const auto exception : exceptions) {
    field(std::to_string(exception));
  }
  const auto digest = QCryptographicHash::hash(QByteArray::fromStdString(canonical),
                                               QCryptographicHash::Sha256);
  return digest.toHex().toStdString();
}

} // namespace pcm::meeting
