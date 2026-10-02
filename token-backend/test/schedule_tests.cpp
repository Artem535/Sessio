#include "schedule/schedule.h"
#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <limits>

namespace {
using namespace pcm::schedule;
int64_t utc(int year, unsigned month, unsigned day, int hour = 0, int minute = 0) {
  using namespace std::chrono;
  return duration_cast<milliseconds>((sys_days{year_month_day{std::chrono::year{year},
      std::chrono::month{month}, std::chrono::day{day}}} + hours{hour} + minutes{minute})
      .time_since_epoch()).count();
}
Snapshot weekly() {
  Snapshot s;
  s.revision = 1;
  s.timezone = "Europe/Moscow";
  s.dtstartLocal = "2026-10-06T18:00:00";
  s.durationSeconds = 3600;
  s.rrule = "FREQ=WEEKLY;INTERVAL=1;BYDAY=TU";
  s.active = s.joinEnabled = true;
  return s;
}

TEST(Schedule, WeeklyOccurrencesHaveDistinctOriginalUtcKeys) {
  const auto s = weekly();
  ASSERT_TRUE(validate(s).valid);
  const auto result = occurrencesBetween(s, utc(2026, 10, 1), utc(2026, 10, 14));
  ASSERT_EQ(result.status, Status::Available);
  ASSERT_EQ(result.occurrences.size(), 2);
  EXPECT_EQ(result.occurrences[0].originalStartMs, utc(2026, 10, 6, 15));
  EXPECT_EQ(result.occurrences[1].originalStartMs, utc(2026, 10, 13, 15));
}

TEST(Schedule, MovedOverrideWinsExceptionAndIsFoundOutsideOriginalRange) {
  auto s = weekly();
  const auto original = utc(2026, 10, 6, 15);
  const auto moved = utc(2027, 2, 1, 15);
  s.exceptions = {original};
  s.overrides = {{original, moved, moved + 3600000, true}};
  EXPECT_TRUE(occurrencesBetween(s, original, original).occurrences.empty());
  const auto joined = resolve(s, moved, 300, 300);
  ASSERT_EQ(joined.status, Status::Available);
  ASSERT_TRUE(joined.occurrence);
  EXPECT_EQ(joined.occurrence->originalStartMs, original);
  EXPECT_EQ(joined.occurrence->startMs, moved);
  s.overrides.front().joinEnabled = false;
  EXPECT_EQ(resolve(s, moved, 300, 300).status, Status::Unavailable);
}

TEST(Schedule, JoinWindowsAreInclusiveAndAmbiguousWindowsNeverSelectOccurrence) {
  auto s = weekly();
  const auto start = utc(2026, 10, 6, 15);
  EXPECT_EQ(resolve(s, start - 300000, 300, 300).status, Status::Available);
  EXPECT_EQ(resolve(s, start - 300001, 300, 300).status, Status::Unavailable);
  EXPECT_EQ(resolve(s, start + 3900000, 300, 300).status, Status::Available);
  EXPECT_EQ(resolve(s, start + 3900001, 300, 300).status, Status::Unavailable);
  s.overrides = {{utc(2026, 10, 13, 15), start, start + 3600000, true}};
  const auto ambiguous = resolve(s, start, 300, 300);
  EXPECT_EQ(ambiguous.status, Status::Ambiguous);
  EXPECT_FALSE(ambiguous.occurrence);
  s.active = false;
  EXPECT_EQ(resolve(s, start, 300, 300).status, Status::Unavailable);
}

TEST(Schedule, SeriesJoinDisabledSuppressesBaseButAllowsExplicitOverride) {
  auto s = weekly();
  const auto start = utc(2026, 10, 6, 15);
  s.joinEnabled = false;
  EXPECT_EQ(resolve(s, start, 0, 0).status, Status::Unavailable);
  s.overrides = {{start, start, start + 3600000, true}};
  EXPECT_EQ(resolve(s, start, 0, 0).status, Status::Available);
  s.exceptions = {start};
  s.overrides.clear();
  EXPECT_EQ(resolve(s, start, 0, 0).status, Status::Unavailable);
  EXPECT_EQ(resolve(s, utc(2026, 10, 13, 15), 0, 0).status, Status::Unavailable);
}

TEST(Schedule, MoscowScheduleIsIndependentOfHostTimezoneAcrossRepeatedRequests) {
  const auto *old = std::getenv("TZ");
  const std::optional<std::string> saved = old ? std::optional<std::string>{old} : std::nullopt;
  for (const auto *host : {"UTC", "America/New_York", "Asia/Tokyo"}) {
    setenv("TZ", host, 1);
    tzset();
    for (int i = 0; i < 3; ++i) {
      const auto r = occurrencesBetween(weekly(), utc(2026, 10, 6), utc(2026, 10, 7));
      ASSERT_EQ(r.occurrences.size(), 1);
      EXPECT_EQ(r.occurrences[0].startMs, utc(2026, 10, 6, 15));
      EXPECT_EQ(r.occurrences[0].originalStartMs, utc(2026, 10, 6, 15));
    }
  }
  if (saved) setenv("TZ", saved->c_str(), 1); else unsetenv("TZ");
  tzset();
}

TEST(Schedule, BerlinDstGapSkipsOccurrenceAndFoldChoosesEarlierUtcInstant) {
  auto s = weekly();
  s.timezone = "Europe/Berlin";
  s.dtstartLocal = "2026-03-22T02:30:00";
  s.rrule = "FREQ=WEEKLY;INTERVAL=1;BYDAY=SU";
  const auto spring = occurrencesBetween(s, utc(2026, 3, 22), utc(2026, 4, 6));
  ASSERT_EQ(spring.occurrences.size(), 2);
  EXPECT_EQ(spring.occurrences[0].startMs, utc(2026, 3, 22, 1, 30));
  EXPECT_EQ(spring.occurrences[1].startMs, utc(2026, 4, 5, 0, 30));
  s.dtstartLocal = "2026-10-18T02:30:00";
  for (int i = 0; i < 3; ++i) {
    const auto autumn = occurrencesBetween(s, utc(2026, 10, 25), utc(2026, 10, 26));
    ASSERT_EQ(autumn.occurrences.size(), 1);
    EXPECT_EQ(autumn.occurrences[0].originalStartMs, utc(2026, 10, 25, 0, 30));
  }
}

TEST(Schedule, MonthlyMissingDayIsSkippedAndUtcUntilIsInclusive) {
  auto s = weekly();
  s.dtstartLocal = "2026-01-31T18:00:00";
  s.rrule = "FREQ=MONTHLY;INTERVAL=1;BYMONTHDAY=31;UNTIL=20260531T150000Z";
  s.untilMs = utc(2026, 5, 31, 15);
  const auto r = occurrencesBetween(s, utc(2026, 1, 1), utc(2026, 7, 1));
  ASSERT_EQ(r.occurrences.size(), 3);
  EXPECT_EQ(r.occurrences[0].startMs, utc(2026, 1, 31, 15));
  EXPECT_EQ(r.occurrences[1].startMs, utc(2026, 3, 31, 15));
  EXPECT_EQ(r.occurrences[2].startMs, utc(2026, 5, 31, 15));
  s.untilMs = utc(2026, 5, 31, 15) - 1000;
  EXPECT_EQ(occurrencesBetween(s, utc(2026, 1, 1), utc(2026, 7, 1)).occurrences.size(), 2);
}

TEST(Schedule, OldInfiniteDailySeriesSeeksWithoutReplayingEntireHistory) {
  auto s = weekly();
  s.timezone = "UTC";
  s.dtstartLocal = "1900-01-01T18:00:00";
  s.rrule = "FREQ=DAILY;INTERVAL=1";
  const auto r = resolve(s, utc(2026, 10, 1, 18), 0, 0);
  ASSERT_EQ(r.status, Status::Available) << r.error;
  ASSERT_TRUE(r.occurrence);
  EXPECT_EQ(r.occurrence->originalStartMs, utc(2026, 10, 1, 18));
  EXPECT_EQ(occurrencesBetween(s, utc(1900, 1, 1), utc(2026, 10, 1)).status,
            Status::BudgetExceeded);
}

TEST(Schedule, UnsupportedAndMalformedSchedulesAreRejectedWithoutPartialOccurrences) {
  for (const auto *rule : {"FREQ=HOURLY", "FREQ=DAILY;COUNT=10", "FREQ=WEEKLY;BYDAY=1MO",
      "FREQ=MONTHLY;BYMONTHDAY=-1", "FREQ=YEARLY;BYMONTH=1", "FREQ=DAILY;INTERVAL=0",
      "FREQ=DAILY;INTERVAL=32768", "FREQ=DAILY;BYHOUR=18", "FREQ=DAILY;FREQ=WEEKLY",
      "FREQ=WEEKLY;BYDAY=MO,MO", "FREQ=DAILY;UNTIL=20260230T000000Z"}) {
    auto s = weekly();
    s.rrule = rule;
    EXPECT_FALSE(validate(s).valid) << rule;
    const auto r = occurrencesBetween(s, utc(2026, 1, 1), utc(2027, 1, 1));
    EXPECT_EQ(r.status, Status::Invalid) << rule;
    EXPECT_TRUE(r.occurrences.empty());
  }
  auto s = weekly();
  s.timezone = "Unknown/Timezone";
  EXPECT_FALSE(validate(s).valid);
  s = weekly();
  s.dtstartLocal = "2026-02-30T18:00:00";
  EXPECT_FALSE(validate(s).valid);
  s = weekly();
  s.exceptions = {utc(2026, 10, 6, 15), utc(2026, 10, 6, 15)};
  EXPECT_FALSE(validate(s).valid);
  s = weekly();
  s.overrides = {{utc(2026, 10, 6, 15), 0, 1000, true},
                 {utc(2026, 10, 6, 15), 0, 1000, false}};
  EXPECT_FALSE(validate(s).valid);
  s = weekly();
  s.durationSeconds = std::numeric_limits<int64_t>::max();
  EXPECT_EQ(resolve(s, utc(2026, 10, 6, 15), 0, 0).status, Status::Invalid);
  s = weekly();
  EXPECT_EQ(resolve(s, 0, std::numeric_limits<int64_t>::max(), 0).status, Status::Invalid);
  EXPECT_EQ(occurrencesBetween(s, std::numeric_limits<int64_t>::min(),
      std::numeric_limits<int64_t>::max()).status, Status::Invalid);
}

TEST(Schedule, LocalTimestampRequiresDigitsAndSecondPrecision) {
  auto s = weekly();
  s.dtstartLocal = "2026-10-06T-0:00:00";
  EXPECT_FALSE(validate(s).valid);
  s = weekly();
  s.untilMs = utc(2026, 10, 6, 15) + 1;
  EXPECT_FALSE(validate(s).valid);
}

TEST(Schedule, SeekingPreservesWeeklyMonthlyAndYearlyIntervalPhase) {
  auto s = weekly();
  s.dtstartLocal = "2000-01-03T18:00:00";
  s.rrule = "FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,WE";
  const auto weeks = occurrencesBetween(s, utc(2026, 9, 28), utc(2026, 10, 12));
  ASSERT_EQ(weeks.occurrences.size(), 2);
  EXPECT_EQ(weeks.occurrences[0].startMs, utc(2026, 10, 5, 15));
  EXPECT_EQ(weeks.occurrences[1].startMs, utc(2026, 10, 7, 15));
  s.dtstartLocal = "2000-01-31T18:00:00";
  s.rrule = "FREQ=MONTHLY;INTERVAL=2;BYMONTHDAY=31";
  const auto months = occurrencesBetween(s, utc(2026, 9, 1), utc(2027, 2, 1));
  ASSERT_EQ(months.occurrences.size(), 1);
  EXPECT_EQ(months.occurrences[0].startMs, utc(2027, 1, 31, 15));
  s.dtstartLocal = "2000-02-29T18:00:00";
  s.rrule = "FREQ=YEARLY;INTERVAL=1;BYMONTH=2;BYMONTHDAY=29";
  const auto years = occurrencesBetween(s, utc(2025, 1, 1), utc(2029, 1, 1));
  ASSERT_EQ(years.occurrences.size(), 1);
  EXPECT_EQ(years.occurrences[0].startMs, utc(2028, 2, 29, 15));
}

TEST(Schedule, LongMovedOverrideIsFoundEvenWhenStartPrecedesBaseLookbackWindow) {
  auto s = weekly();
  const auto now = utc(2026, 10, 9, 15);
  s.overrides = {{utc(2026, 10, 6, 15), now - 3 * 86400000LL, now + 1000, true}};
  const auto r = resolve(s, now, 0, 0);
  ASSERT_EQ(r.status, Status::Available);
  EXPECT_EQ(r.occurrence->originalStartMs, utc(2026, 10, 6, 15));
}

// A missing or empty zoneinfo directory must make named zones fail validation,
// never succeed with other data. Packaged desktop builds depend on this: the
// shipped data directory is the only source of zones.
TEST(Schedule, MissingZoneinfoDirectoryRejectsNamedZonesAndRestoreWorks) {
  const auto original = zoneinfoDirectory();
  ASSERT_TRUE(zoneinfoDirectoryHasData(original));
  ASSERT_TRUE(validate(weekly()).valid);

  EXPECT_FALSE(setZoneinfoDirectory("/nonexistent/pcm-zoneinfo"));
  const auto broken = validate(weekly());
  EXPECT_FALSE(broken.valid);
  EXPECT_EQ(broken.error, "unknown timezone");
  auto utcSnapshot = weekly();
  utcSnapshot.timezone = "UTC"; // the built-in UTC zone needs no data files
  EXPECT_TRUE(validate(utcSnapshot).valid);

  EXPECT_TRUE(setZoneinfoDirectory(original));
  EXPECT_TRUE(validate(weekly()).valid);
}

TEST(Schedule, ZoneinfoDirectoryWithoutDataIsNotReportedAsData) {
  EXPECT_FALSE(zoneinfoDirectoryHasData("/nonexistent/pcm-zoneinfo"));
  EXPECT_FALSE(zoneinfoDirectoryHasData(""));
}
}
