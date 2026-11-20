#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "schedule_snapshot.h"

namespace {

using pcm::database::ScheduleSource;
using namespace pcm::meeting;

constexpr int64_t kHour = 3600000;
constexpr int64_t kStart = 1791309600000; // 2026-10-06T18:00:00Z, 21:00 Moscow

ScheduleSource source() {
  ScheduleSource s;
  s.series.id = 42;
  s.series.name = std::string{"Anna K. therapy"};
  s.series.description = std::string{"private note about diagnosis"};
  s.series.client_id = 7;
  s.series.client_name = std::string{"Anna Karenina"};
  s.series.cost = 5000.0;
  s.series.event_stat_id = 1;
  s.series.payment_stat_id = 2;
  s.series.start_date = kStart;
  s.series.end_date = kStart + kHour;
  s.series.recurrence_rule = "FREQ=WEEKLY;INTERVAL=1;BYDAY=TU";
  s.series.active = true;
  s.identity.series_uid = "11111111-2222-4333-8444-555555555555";
  s.identity.timezone = "Europe/Moscow";
  return s;
}

QJsonObject parse(const std::optional<std::string> &payload) {
  EXPECT_TRUE(payload.has_value());
  return QJsonDocument::fromJson(QByteArray::fromStdString(payload.value_or("{}"))).object();
}

} // namespace

TEST(ScheduleSnapshotTest, ExportsExplicitTimezoneAndLocalStartWithoutOffset) {
  const auto json = parse(buildScheduleSnapshotPayload(source()));
  EXPECT_EQ(json["schema_version"].toInt(), 1);
  EXPECT_EQ(json["timezone"].toString(), "Europe/Moscow");
  EXPECT_EQ(json["dtstart_local"].toString(), "2026-10-06T21:00:00");
  EXPECT_EQ(json["duration_seconds"].toInt(), 3600);
  EXPECT_EQ(json["rrule"].toString(), "FREQ=WEEKLY;INTERVAL=1;BYDAY=TU");
  EXPECT_TRUE(json["until_utc"].isNull());
  EXPECT_TRUE(json["active"].toBool());
  EXPECT_TRUE(json["join_enabled"].toBool());
  EXPECT_TRUE(json["overrides"].toArray().isEmpty());
  EXPECT_TRUE(json["exceptions"].toArray().isEmpty());
}

TEST(ScheduleSnapshotTest, ExportContainsNoPersonalDataOrLocalIds) {
  const auto payload = buildScheduleSnapshotPayload(source());
  ASSERT_TRUE(payload.has_value());
  for (const char *forbidden : {"Anna", "Karenina", "therapy", "diagnosis", "5000", "client",
                                "name", "description", "cost", "\"id\"", "series_id"}) {
    EXPECT_EQ(payload->find(forbidden), std::string::npos) << forbidden;
  }
  const auto json = parse(payload);
  EXPECT_EQ(json.keys().size(), 12);
}

TEST(ScheduleSnapshotTest, StatusesCanceledNoShowRescheduledDisableJoin) {
  for (const int64_t status : {3, 5, 6}) {
    auto s = source();
    s.overrides.push_back({kStart + 7 * 24 * kHour, kStart + 7 * 24 * kHour,
                           kStart + 7 * 24 * kHour + kHour, status});
    const auto json = parse(buildScheduleSnapshotPayload(s));
    const auto overrides = json["overrides"].toArray();
    ASSERT_EQ(overrides.size(), 1) << status;
    EXPECT_FALSE(overrides[0].toObject()["join_enabled"].toBool()) << status;
    EXPECT_TRUE(json["join_enabled"].toBool());
  }
  for (const int64_t status : {1, 2, 4}) {
    auto s = source();
    s.overrides.push_back({kStart + 7 * 24 * kHour, kStart + 8 * 24 * kHour,
                           kStart + 8 * 24 * kHour + kHour, status});
    const auto json = parse(buildScheduleSnapshotPayload(s));
    EXPECT_TRUE(json["overrides"].toArray()[0].toObject()["join_enabled"].toBool()) << status;
  }
}

TEST(ScheduleSnapshotTest, WholeSeriesCancelDisablesJoinAndPropagatesToOverrides) {
  auto s = source();
  s.series.event_stat_id = 3;
  s.overrides.push_back({kStart + 7 * 24 * kHour, kStart + 8 * 24 * kHour,
                         kStart + 8 * 24 * kHour + kHour, 1});
  const auto json = parse(buildScheduleSnapshotPayload(s));
  EXPECT_FALSE(json["join_enabled"].toBool());
  EXPECT_FALSE(json["overrides"].toArray()[0].toObject()["join_enabled"].toBool());
}

TEST(ScheduleSnapshotTest, DeactivatedSeriesAndUntilAndExceptionsAreFullSnapshot) {
  auto s = source();
  s.series.active = false;
  s.series.recurrence_until = kStart + 30 * 24 * kHour + 999;
  s.exceptions = {kStart + 14 * 24 * kHour, kStart + 7 * 24 * kHour, kStart + 7 * 24 * kHour};
  const auto json = parse(buildScheduleSnapshotPayload(s));
  EXPECT_FALSE(json["active"].toBool());
  EXPECT_EQ(json["until_utc"].toString(), "2026-11-05T18:00:00Z");
  const auto exceptions = json["exceptions"].toArray();
  ASSERT_EQ(exceptions.size(), 2); // sorted and de-duplicated
  EXPECT_EQ(exceptions[0].toString(), "2026-10-13T18:00:00Z");
  EXPECT_EQ(exceptions[1].toString(), "2026-10-20T18:00:00Z");
}

TEST(ScheduleSnapshotTest, OverrideTimesUseUtcSecondsKeyedByOriginalStart) {
  auto s = source();
  s.overrides.push_back({kStart + 7 * 24 * kHour + 123, kStart + 8 * 24 * kHour + 456,
                         kStart + 8 * 24 * kHour + kHour + 789, 1});
  const auto o = parse(buildScheduleSnapshotPayload(s))["overrides"].toArray()[0].toObject();
  EXPECT_EQ(o["original_start_utc"].toString(), "2026-10-13T18:00:00Z");
  EXPECT_EQ(o["start_utc"].toString(), "2026-10-14T18:00:00Z");
  EXPECT_EQ(o["end_utc"].toString(), "2026-10-14T19:00:00Z");
}

TEST(ScheduleSnapshotTest, UnknownTimezoneIsNotGuessed) {
  auto s = source();
  s.identity.timezone = "Not/AZone";
  EXPECT_FALSE(buildScheduleSnapshotPayload(s).has_value());
  s.identity.timezone.clear();
  EXPECT_FALSE(buildScheduleSnapshotPayload(s).has_value());
}

TEST(ScheduleSnapshotTest, WirePayloadCarriesRevisionAndRoundTripsThroughParser) {
  const auto pending = buildScheduleSnapshotPayload(source());
  ASSERT_TRUE(pending.has_value());
  auto snapshot = parseSnapshot(QByteArray::fromStdString(*pending));
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_EQ(snapshot->revision, 0);
  snapshot->revision = 3;
  snapshot->baseRevision = 2;
  const auto wire = serializeSnapshot(*snapshot);
  const auto json = QJsonDocument::fromJson(wire).object();
  EXPECT_EQ(json["revision"].toInt(), 3);
  EXPECT_EQ(json["base_revision"].toInt(), 2);
  const auto reparsed = parseSnapshot(wire);
  ASSERT_TRUE(reparsed.has_value());
  EXPECT_EQ(serializeSnapshot(*reparsed), wire);
}

TEST(ScheduleSnapshotTest, ParserRejectsMissingOrMistypedFields) {
  EXPECT_FALSE(parseSnapshot("{}").has_value());
  EXPECT_FALSE(parseSnapshot("not json").has_value());
  auto json = QJsonDocument::fromJson(
                  QByteArray::fromStdString(*buildScheduleSnapshotPayload(source())))
                  .object();
  json.remove("join_enabled"); // mandatory in version 1
  EXPECT_FALSE(parseSnapshot(QJsonDocument(json).toJson()).has_value());
}

TEST(ScheduleSnapshotTest, ContentHashIgnoresRevisionAndMatchesBackendCanonicalForm) {
  auto snapshot = *parseSnapshot(QByteArray::fromStdString(*buildScheduleSnapshotPayload(source())));
  const auto hash = scheduleContentHash(snapshot);
  EXPECT_EQ(hash.size(), 64u);
  snapshot.revision = 9;
  snapshot.baseRevision = 8;
  EXPECT_EQ(scheduleContentHash(snapshot), hash);
  snapshot.joinEnabled = false;
  EXPECT_NE(scheduleContentHash(snapshot), hash);

  // Order of overrides/exceptions does not change the canonical hash.
  pcm::schedule::Snapshot a;
  a.timezone = "Europe/Moscow";
  a.dtstartLocal = "2026-10-06T21:00:00";
  a.durationSeconds = 3600;
  a.rrule = "FREQ=WEEKLY;INTERVAL=1;BYDAY=TU";
  a.active = true;
  a.joinEnabled = true;
  a.exceptions = {20, 10};
  auto b = a;
  b.exceptions = {10, 20};
  EXPECT_EQ(scheduleContentHash(a), scheduleContentHash(b));
  // Golden value: SHA-256 of the backend's length-prefixed canonical string
  // (computed independently with hashlib from the documented algorithm).
  EXPECT_EQ(scheduleContentHash(a), "1ca6d6821028e5009ac9160d2a7ce4fe51ab93fab79c2b12e8a738a98bef5cc1");
}
