#include <Poco/File.h>
#include <Poco/Path.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include <QTimeZone>

#include "config.h"
#include "database.h"
#include "recurrence_utils.h"

namespace {
DuckEvent eventAt(const int64_t id, const int64_t startMs, const int64_t eventStatId = 1) {
  DuckEvent event;
  event.id = id;
  event.start_date = startMs;
  event.event_stat_id = eventStatId;
  return event;
}

DuckEvent workSessionAt(const int64_t id, const int64_t startMs, const int64_t endMs,
                       const QString &clientName = QStringLiteral("Client"),
                       const int64_t eventStatId = 1) {
  DuckEvent event;
  event.id = id;
  event.is_work_event = true;
  event.start_date = startMs;
  event.end_date = endMs;
  event.event_stat_id = eventStatId;
  if (!clientName.isEmpty()) {
    event.client_name = clientName.toStdString();
  }
  return event;
}
} // namespace

TEST(RecurrenceUtilsTest, EventsForClientCombinesMaterializedAndVirtualOccurrences) {
  pcm::config::Config conf{
      .db_conf = pcm::config::DatabaseConfig{
          .db_pth = Poco::Path(Poco::Path::current()).append("tmp_dir_events_for_client_recurrence")}};

  auto db_dir = Poco::File(conf.db_conf().db_pth);
  if (db_dir.exists()) {
    db_dir.remove(true);
  }

  pcm::database::Database db{conf};

  DuckClient client;
  client.name = std::string{"Eve"};
  client.last_name = std::string{"E"};
  const auto clientId = db.add_client(client);
  ASSERT_GT(clientId, 0);

  const auto windowStart = QDateTime::currentDateTime().addMonths(-3);
  const auto windowEnd = QDateTime::currentDateTime().addMonths(3);

  DuckEvent standalone;
  standalone.name = std::string{"One-off session"};
  standalone.start_date = QDateTime::currentDateTime().addDays(1).toUTC().toMSecsSinceEpoch();
  standalone.end_date = *standalone.start_date + 3'600'000;
  const auto standaloneId = db.add_event(standalone);
  ASSERT_GT(standaloneId, 0);
  ASSERT_GT(db.add_event_client(standaloneId, clientId), 0);

  DuckEventSeries series;
  series.name = std::string{"Weekly with Eve"};
  series.client_id = clientId;
  series.start_date = QDateTime::currentDateTime().addDays(2).toUTC().toMSecsSinceEpoch();
  series.end_date = *series.start_date + 3'600'000;
  series.duration = 3600;
  series.recurrence_rule =
      pcm::recurrence::weeklyRuleForDate(QDateTime::currentDateTime().addDays(2).date()).toStdString();
  const auto seriesId = db.add_event_series(series);
  ASSERT_GT(seriesId, 0);

  const auto events =
      pcm::recurrence::eventsForClient(db, clientId, windowStart, windowEnd);

  const auto standaloneCount =
      std::count_if(events.begin(), events.end(),
                    [standaloneId](const DuckEvent &e) { return e.id == standaloneId; });
  EXPECT_EQ(standaloneCount, 1);

  const auto virtualCount =
      std::count_if(events.begin(), events.end(),
                    [seriesId](const DuckEvent &e) {
                      return e.is_virtual_occurrence && e.series_id == seriesId;
                    });
  EXPECT_GE(virtualCount, 1);

  db_dir.remove(true);
}

TEST(RecurrenceUtilsTest, EventsForClientSkipsMaterializedOccurrenceOfSameSeries) {
  pcm::config::Config conf{
      .db_conf = pcm::config::DatabaseConfig{
          .db_pth = Poco::Path(Poco::Path::current()).append("tmp_dir_events_for_client_materialized")}};

  auto db_dir = Poco::File(conf.db_conf().db_pth);
  if (db_dir.exists()) {
    db_dir.remove(true);
  }

  pcm::database::Database db{conf};

  DuckClient client;
  client.name = std::string{"Frank"};
  client.last_name = std::string{"F"};
  const auto clientId = db.add_client(client);
  ASSERT_GT(clientId, 0);

  const auto seriesStartLocal =
      QDateTime(QDate::currentDate().addDays(1), QTime(10, 0, 0));
  DuckEventSeries series;
  series.name = std::string{"Weekly with Frank"};
  series.client_id = clientId;
  series.start_date = seriesStartLocal.toUTC().toMSecsSinceEpoch();
  series.end_date = *series.start_date + 3'600'000;
  series.duration = 3600;
  series.recurrence_rule =
      pcm::recurrence::weeklyRuleForDate(seriesStartLocal.date()).toStdString();
  const auto seriesId = db.add_event_series(series);
  ASSERT_GT(seriesId, 0);

  DuckEvent materialized;
  materialized.name = std::string{"Weekly with Frank"};
  materialized.start_date = seriesStartLocal.toUTC().toMSecsSinceEpoch();
  materialized.end_date = *materialized.start_date + 3'600'000;
  materialized.series_id = seriesId;
  materialized.original_occurrence_start = materialized.start_date;
  const auto materializedId = db.add_event(materialized);
  ASSERT_GT(materializedId, 0);
  ASSERT_GT(db.add_event_client(materializedId, clientId), 0);

  const auto windowStart = QDateTime::currentDateTime().addMonths(-3);
  const auto windowEnd = QDateTime::currentDateTime().addMonths(3);
  const auto events =
      pcm::recurrence::eventsForClient(db, clientId, windowStart, windowEnd);

  const auto occurrencesAtSeriesStart =
      std::count_if(events.begin(), events.end(), [&](const DuckEvent &e) {
        return e.start_date.has_value() &&
               std::abs(*e.start_date - *materialized.start_date) < 1000;
      });
  EXPECT_EQ(occurrencesAtSeriesStart, 1);

  db_dir.remove(true);
}

TEST(RecurrenceUtilsTest, LastAndNextAppointmentPastOnlyLeavesNextEmpty) {
  const QVector<DuckEvent> events{eventAt(1, 1'000)};
  const auto result = pcm::recurrence::lastAndNextAppointment(events, 2'000);
  ASSERT_TRUE(result.last.has_value());
  EXPECT_EQ(result.last->id, 1);
  EXPECT_FALSE(result.next.has_value());
}

TEST(RecurrenceUtilsTest, LastAndNextAppointmentFutureOnlyLeavesLastEmpty) {
  const QVector<DuckEvent> events{eventAt(1, 3'000)};
  const auto result = pcm::recurrence::lastAndNextAppointment(events, 2'000);
  EXPECT_FALSE(result.last.has_value());
  ASSERT_TRUE(result.next.has_value());
  EXPECT_EQ(result.next->id, 1);
}

TEST(RecurrenceUtilsTest, LastAndNextAppointmentSkipsCanceledEvents) {
  const QVector<DuckEvent> events{
      eventAt(1, 2'500, /*event_stat_id=*/3), // canceled, closer to now
      eventAt(2, 3'000, /*event_stat_id=*/1), // scheduled, farther
  };
  const auto result = pcm::recurrence::lastAndNextAppointment(events, 2'000);
  ASSERT_TRUE(result.next.has_value());
  EXPECT_EQ(result.next->id, 2);
}

TEST(RecurrenceUtilsTest, ResolveNoteLinkFindsMaterializedEventById) {
  pcm::config::Config conf{
      .db_conf = pcm::config::DatabaseConfig{
          .db_pth = Poco::Path(Poco::Path::current()).append("tmp_dir_resolve_link_by_id")}};

  auto db_dir = Poco::File(conf.db_conf().db_pth);
  if (db_dir.exists()) {
    db_dir.remove(true);
  }

  pcm::database::Database db{conf};

  DuckEvent event;
  event.name = std::string{"Session"};
  event.start_date = 1730000000000;
  event.end_date = 1730003600000;
  const auto eventId = db.add_event(event);
  ASSERT_GT(eventId, 0);

  DuckClientNote note;
  note.linked_event_id = eventId;

  const auto resolved = pcm::recurrence::resolveNoteLink(db, note);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->id, eventId);

  db_dir.remove(true);
}

TEST(RecurrenceUtilsTest, ResolveNoteLinkRebuildsUnmaterializedVirtualOccurrence) {
  pcm::config::Config conf{
      .db_conf = pcm::config::DatabaseConfig{
          .db_pth = Poco::Path(Poco::Path::current()).append("tmp_dir_resolve_link_virtual")}};

  auto db_dir = Poco::File(conf.db_conf().db_pth);
  if (db_dir.exists()) {
    db_dir.remove(true);
  }

  pcm::database::Database db{conf};

  const auto seriesStartLocal = QDateTime(QDate::currentDate().addDays(1), QTime(10, 0, 0));
  DuckEventSeries series;
  series.name = std::string{"Weekly"};
  series.start_date = seriesStartLocal.toUTC().toMSecsSinceEpoch();
  series.end_date = *series.start_date + 3'600'000;
  series.duration = 3600;
  series.recurrence_rule = pcm::recurrence::weeklyRuleForDate(seriesStartLocal.date()).toStdString();
  const auto seriesId = db.add_event_series(series);
  ASSERT_GT(seriesId, 0);

  DuckClientNote note;
  note.linked_series_id = seriesId;
  note.linked_occurrence_start_ms = *series.start_date;

  const auto resolved = pcm::recurrence::resolveNoteLink(db, note);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_TRUE(resolved->is_virtual_occurrence);
  EXPECT_EQ(resolved->series_id, seriesId);
  EXPECT_EQ(*resolved->start_date, *series.start_date);

  db_dir.remove(true);
}

TEST(RecurrenceUtilsTest, BuildVirtualOccurrenceCopiesProviderFieldsFromSeries) {
  DuckEventSeries series;
  series.name = std::string{"Weekly Session"};
  series.start_date = 1730000000000;
  series.duration = 3600;
  series.provider_kind = std::string{"ExternalUrl"};
  series.meeting_ref = std::string{"https://meet.example.invalid/room-3"};
  series.invitation_state = std::nullopt;

  const auto occurrenceStart =
      QDateTime::fromMSecsSinceEpoch(*series.start_date, QTimeZone::UTC);
  const auto occurrence = pcm::recurrence::buildVirtualOccurrence(series, occurrenceStart, -1);

  ASSERT_TRUE(occurrence.provider_kind.has_value());
  EXPECT_EQ(*occurrence.provider_kind, "ExternalUrl");
  ASSERT_TRUE(occurrence.meeting_ref.has_value());
  EXPECT_EQ(*occurrence.meeting_ref, "https://meet.example.invalid/room-3");
  EXPECT_FALSE(occurrence.invitation_state.has_value());
}

TEST(RecurrenceUtilsTest, ResolveNoteLinkFindsMaterializedRowForSinceMaterializedOccurrence) {
  pcm::config::Config conf{
      .db_conf = pcm::config::DatabaseConfig{
          .db_pth = Poco::Path(Poco::Path::current()).append("tmp_dir_resolve_link_graduated")}};

  auto db_dir = Poco::File(conf.db_conf().db_pth);
  if (db_dir.exists()) {
    db_dir.remove(true);
  }

  pcm::database::Database db{conf};

  const auto seriesStartLocal = QDateTime(QDate::currentDate().addDays(1), QTime(10, 0, 0));
  DuckEventSeries series;
  series.name = std::string{"Weekly"};
  series.start_date = seriesStartLocal.toUTC().toMSecsSinceEpoch();
  series.end_date = *series.start_date + 3'600'000;
  series.duration = 3600;
  series.recurrence_rule = pcm::recurrence::weeklyRuleForDate(seriesStartLocal.date()).toStdString();
  const auto seriesId = db.add_event_series(series);
  ASSERT_GT(seriesId, 0);

  DuckEvent materialized;
  materialized.name = std::string{"Weekly (materialized)"};
  materialized.start_date = *series.start_date;
  materialized.end_date = *materialized.start_date + 3'600'000;
  materialized.series_id = seriesId;
  materialized.original_occurrence_start = materialized.start_date;
  const auto materializedId = db.add_event(materialized);
  ASSERT_GT(materializedId, 0);

  DuckClientNote note;
  note.linked_series_id = seriesId;
  note.linked_occurrence_start_ms = *series.start_date;

  const auto resolved = pcm::recurrence::resolveNoteLink(db, note);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_FALSE(resolved->is_virtual_occurrence);
  EXPECT_EQ(resolved->id, materializedId);

  db_dir.remove(true);
}

TEST(RecurrenceUtilsTest, ComputeDaySummaryEmptyDayHasNoSessions) {
  const auto date = QDate::currentDate();
  const auto noon =
      QDateTime(date, QTime(12, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();

  const auto summary = pcm::recurrence::computeDaySummary(
      {}, {}, QTime(9, 0), QTime(18, 0), date, noon, 50);

  EXPECT_FALSE(summary.hasSessions);
  EXPECT_EQ(summary.sessionCount, 0);
  EXPECT_EQ(summary.clientCount, 0);
  EXPECT_EQ(summary.busyMinutes, 0);
  EXPECT_FALSE(summary.nextSession.has_value());
  EXPECT_FALSE(summary.freeWindowStart.has_value());
  EXPECT_TRUE(summary.upcoming.isEmpty());
}

TEST(RecurrenceUtilsTest, ComputeDaySummaryFullyBookedDayHasNoFreeWindow) {
  const auto date = QDate::currentDate();
  const auto startMs =
      QDateTime(date, QTime(9, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  const auto endMs =
      QDateTime(date, QTime(18, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  const auto beforeStartMs =
      QDateTime(date, QTime(8, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();

  const QVector<DuckEvent> events{workSessionAt(1, startMs, endMs)};
  const QVector<QPair<QDateTime, QDateTime>> busy{
      {QDateTime::fromMSecsSinceEpoch(startMs, QTimeZone::systemTimeZone()),
       QDateTime::fromMSecsSinceEpoch(endMs, QTimeZone::systemTimeZone())}};

  const auto summary = pcm::recurrence::computeDaySummary(
      events, busy, QTime(9, 0), QTime(18, 0), date, beforeStartMs, 50);

  EXPECT_TRUE(summary.hasSessions);
  EXPECT_FALSE(summary.freeWindowStart.has_value());
  EXPECT_FALSE(summary.freeWindowEnd.has_value());
}

TEST(RecurrenceUtilsTest, ComputeDaySummaryFreeWindowRespectsBusyIntervalsNotJustEvents) {
  const auto date = QDate::currentDate();
  const auto eventStartMs =
      QDateTime(date, QTime(10, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  const auto eventEndMs =
      QDateTime(date, QTime(11, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  const auto beforeMs =
      QDateTime(date, QTime(8, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();

  const QVector<DuckEvent> events{workSessionAt(1, eventStartMs, eventEndMs)};
  // Busy interval starts at the work day's start (no gap ahead of it) and
  // extends well past the event's own 10:00-11:00 bounds (buffer / personal
  // event) - proving the search follows busyIntervals, not the event's own
  // start/end.
  const QVector<QPair<QDateTime, QDateTime>> busy{
      {QDateTime(date, QTime(9, 0, 0), QTimeZone::systemTimeZone()),
       QDateTime(date, QTime(14, 0, 0), QTimeZone::systemTimeZone())}};

  const auto summary = pcm::recurrence::computeDaySummary(
      events, busy, QTime(9, 0), QTime(18, 0), date, beforeMs, 50);

  ASSERT_TRUE(summary.freeWindowStart.has_value());
  EXPECT_GE(summary.freeWindowStart->time(), QTime(14, 0, 0));
}

TEST(RecurrenceUtilsTest, ComputeDaySummaryExcludesCanceledFromCountsButKeepsItBlocking) {
  const auto date = QDate::currentDate();
  // A qualifying (scheduled) event early in the day, purely so hasSessions
  // is true - its own slot is already in the past by `now` below, so it
  // can't itself supply the free window this test checks for.
  const auto qualifyingStartMs =
      QDateTime(date, QTime(9, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  const auto qualifyingEndMs =
      QDateTime(date, QTime(9, 15, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  // A canceled event later in the day - excluded from counts, but its
  // interval is still passed via busyIntervals (exactly what
  // QEventInfoPage::currentBusyIntervals() does today - it doesn't filter
  // by status).
  const auto canceledStartMs =
      QDateTime(date, QTime(10, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  const auto canceledEndMs =
      QDateTime(date, QTime(11, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  // "Now" sits inside the canceled event's slot, so there's no earlier gap
  // for the search to find - the only candidate gap is whatever remains
  // after the canceled interval ends.
  const auto nowMs =
      QDateTime(date, QTime(10, 30, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();

  const QVector<DuckEvent> events{
      workSessionAt(1, qualifyingStartMs, qualifyingEndMs),
      workSessionAt(2, canceledStartMs, canceledEndMs, QStringLiteral("Anna"),
                    /*eventStatId=*/3)};
  const QVector<QPair<QDateTime, QDateTime>> busy{
      {QDateTime::fromMSecsSinceEpoch(qualifyingStartMs, QTimeZone::systemTimeZone()),
       QDateTime::fromMSecsSinceEpoch(qualifyingEndMs, QTimeZone::systemTimeZone())},
      {QDateTime::fromMSecsSinceEpoch(canceledStartMs, QTimeZone::systemTimeZone()),
       QDateTime::fromMSecsSinceEpoch(canceledEndMs, QTimeZone::systemTimeZone())}};

  const auto summary = pcm::recurrence::computeDaySummary(
      events, busy, QTime(9, 0), QTime(18, 0), date, nowMs, 50);

  EXPECT_TRUE(summary.hasSessions);
  EXPECT_EQ(summary.sessionCount, 1); // canceled event not counted
  EXPECT_EQ(summary.busyMinutes, 15); // only the qualifying event's span
  // The canceled event's interval still blocks the free-window search past
  // its own end time, even though it doesn't count as a session.
  ASSERT_TRUE(summary.freeWindowStart.has_value());
  EXPECT_GE(summary.freeWindowStart->time(), QTime(11, 0, 0));
}

TEST(RecurrenceUtilsTest, ComputeDaySummaryPastDayOmitsNextSessionAndFreeWindow) {
  const auto date = QDate::currentDate().addDays(-1);
  const auto startMs =
      QDateTime(date, QTime(10, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  const auto endMs =
      QDateTime(date, QTime(11, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  const auto nowMs = QDateTime::currentDateTime().toUTC().toMSecsSinceEpoch();

  const QVector<DuckEvent> events{workSessionAt(1, startMs, endMs)};

  const auto summary = pcm::recurrence::computeDaySummary(
      events, {}, QTime(9, 0), QTime(18, 0), date, nowMs, 50);

  EXPECT_TRUE(summary.hasSessions);
  EXPECT_FALSE(summary.nextSession.has_value());
  EXPECT_FALSE(summary.freeWindowStart.has_value());
  EXPECT_EQ(summary.upcoming.size(), 1);
}

TEST(RecurrenceUtilsTest, ComputeDaySummaryTodayAfterWorkHoursOmitsNextAndFreeWindow) {
  const auto date = QDate::currentDate();
  const auto startMs =
      QDateTime(date, QTime(10, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  const auto endMs =
      QDateTime(date, QTime(11, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  const auto lateNowMs =
      QDateTime(date, QTime(23, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();

  const QVector<DuckEvent> events{workSessionAt(1, startMs, endMs)};

  const auto summary = pcm::recurrence::computeDaySummary(
      events, {}, QTime(9, 0), QTime(18, 0), date, lateNowMs, 50);

  EXPECT_FALSE(summary.nextSession.has_value());
  EXPECT_FALSE(summary.freeWindowStart.has_value());
  EXPECT_EQ(summary.upcoming.size(), 1);
}

TEST(RecurrenceUtilsTest, ComputeDaySummaryUpcomingCappedAtThreeChronological) {
  const auto date = QDate::currentDate();
  const auto beforeMs =
      QDateTime(date, QTime(8, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();

  QVector<DuckEvent> events;
  for (int hour = 9; hour < 14; ++hour) {
    const auto startMs =
        QDateTime(date, QTime(hour, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
    const auto endMs =
        QDateTime(date, QTime(hour, 30, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
    events.append(workSessionAt(hour, startMs, endMs));
  }

  const auto summary = pcm::recurrence::computeDaySummary(
      events, {}, QTime(9, 0), QTime(18, 0), date, beforeMs, 50);

  ASSERT_EQ(summary.upcoming.size(), 3);
  EXPECT_EQ(summary.upcoming[0].id, 9);
  EXPECT_EQ(summary.upcoming[1].id, 10);
  EXPECT_EQ(summary.upcoming[2].id, 11);
}

TEST(RecurrenceUtilsTest, ComputeDaySummaryClientCountCountsDistinctClientsNotSessions) {
  const auto date = QDate::currentDate();
  const auto beforeMs =
      QDateTime(date, QTime(8, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  const auto mkTimes = [&](const int hour) {
    return std::make_pair(
        QDateTime(date, QTime(hour, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch(),
        QDateTime(date, QTime(hour, 30, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch());
  };
  const auto [start1, end1] = mkTimes(9);
  const auto [start2, end2] = mkTimes(10);
  const auto [start3, end3] = mkTimes(11);

  const QVector<DuckEvent> events{
      workSessionAt(1, start1, end1, QStringLiteral("Anna")),
      workSessionAt(2, start2, end2, QStringLiteral("Anna")),
      workSessionAt(3, start3, end3, QStringLiteral("")),
  };

  const auto summary = pcm::recurrence::computeDaySummary(
      events, {}, QTime(9, 0), QTime(18, 0), date, beforeMs, 50);

  EXPECT_EQ(summary.sessionCount, 3);
  EXPECT_EQ(summary.clientCount, 2); // "Anna" once + 1 unnamed
}

TEST(RecurrenceUtilsTest, ComputeDaySummarySkipsTooSmallGapAndFindsLargerOneAfter) {
  const auto date = QDate::currentDate();
  const auto beforeMs =
      QDateTime(date, QTime(8, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  const auto session1StartMs =
      QDateTime(date, QTime(8, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  const auto session1EndMs =
      QDateTime(date, QTime(9, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  const auto session2StartMs =
      QDateTime(date, QTime(9, 15, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  const auto session2EndMs =
      QDateTime(date, QTime(10, 0, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();

  const QVector<DuckEvent> events{
      workSessionAt(1, session1StartMs, session1EndMs),
      workSessionAt(2, session2StartMs, session2EndMs),
  };
  const QVector<QPair<QDateTime, QDateTime>> busy{
      {QDateTime::fromMSecsSinceEpoch(session1StartMs, QTimeZone::systemTimeZone()),
       QDateTime::fromMSecsSinceEpoch(session1EndMs, QTimeZone::systemTimeZone())},
      {QDateTime::fromMSecsSinceEpoch(session2StartMs, QTimeZone::systemTimeZone()),
       QDateTime::fromMSecsSinceEpoch(session2EndMs, QTimeZone::systemTimeZone())},
  };

  // minFreeWindowMinutes=50 means we need at least 50 minutes free.
  // The 15-minute gap (9:00-9:15) is too small and should be skipped by the algorithm.
  // The gap after 10:00 until work day ends (18:00) is 8 hours = 480 minutes, which qualifies.
  // This test exercises the "skip too-small gap, keep scanning" path in the free-window search.
  const auto summary = pcm::recurrence::computeDaySummary(
      events, busy, QTime(8, 0), QTime(18, 0), date, beforeMs, 50);

  ASSERT_TRUE(summary.freeWindowStart.has_value());
  ASSERT_TRUE(summary.freeWindowEnd.has_value());
  // The free window should start at or after 10:00, not in the small 9:00-9:15 gap
  EXPECT_GE(summary.freeWindowStart->time(), QTime(10, 0, 0));
  // And end at the work day's end
  EXPECT_EQ(summary.freeWindowEnd->time(), QTime(18, 0, 0));
}

// --- Pinned-timezone (published) series use the shared schedule calculation ---

#include "schedule_snapshot.h"

namespace {
struct PinnedSeriesFixture {
  PinnedSeriesFixture()
      : conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(Poco::Path::current())
                                                                  .append("tmp_dir_pinned_series")}} {
    if (auto dir = Poco::File(conf.db_conf().db_pth); dir.exists()) {
      dir.remove(true);
    }
    db = std::make_unique<pcm::database::Database>(conf);
  }
  ~PinnedSeriesFixture() {
    db.reset();
    if (auto dir = Poco::File(conf.db_conf().db_pth); dir.exists()) {
      dir.remove(true);
    }
  }

  // Tuesdays 18:00 Europe/Berlin, first one 2026-10-20 (CEST, 16:00Z).
  DuckEventSeries berlinWeekly() const {
    DuckEventSeries series;
    series.name = std::string{"Weekly"};
    series.event_stat_id = 1;
    series.payment_stat_id = 1;
    series.start_date = 1792512000000; // 2026-10-20T16:00:00Z
    series.end_date = *series.start_date + 3'600'000;
    series.duration = 3600;
    series.recurrence_rule = "FREQ=WEEKLY;INTERVAL=1;BYDAY=TU";
    return series;
  }

  pcm::config::Config conf;
  std::unique_ptr<pcm::database::Database> db;
};

QString utc(const QDateTime &value) {
  return value.toUTC().toString(Qt::ISODate);
}
} // namespace

TEST(RecurrenceUtilsTest, PinnedTimezoneSeriesKeepsItsWallClockAcrossDaylightSavingChange) {
  PinnedSeriesFixture fixture;
  auto series = fixture.berlinWeekly();
  const auto commit = fixture.db->commit_schedule_change(
      [&]() -> std::optional<int64_t> { return fixture.db->add_event_series(series); },
      "Europe/Berlin", pcm::meeting::buildScheduleSnapshotPayload);
  ASSERT_TRUE(commit.has_value());
  series.id = commit->series_id;

  const auto from = QDateTime::fromMSecsSinceEpoch(1792454400000, QTimeZone::UTC); // 2026-10-20T00:00Z
  const auto to = from.addDays(15);
  const auto starts = pcm::recurrence::seriesOccurrences(*fixture.db, series, from, to);

  ASSERT_EQ(starts.size(), 3);
  EXPECT_EQ(utc(starts[0]), "2026-10-20T16:00:00Z"); // CEST
  EXPECT_EQ(utc(starts[1]), "2026-10-27T17:00:00Z"); // CET after 25 Oct: still 18:00 in Berlin
  EXPECT_EQ(utc(starts[2]), "2026-11-03T17:00:00Z");
}

TEST(RecurrenceUtilsTest, SeriesWithoutPinnedTimezoneKeepsLegacyCalendar) {
  PinnedSeriesFixture fixture;
  auto series = fixture.berlinWeekly();
  const auto seriesId = fixture.db->add_event_series(series);
  ASSERT_GT(seriesId, 0);
  series.id = seriesId;

  const auto from = QDateTime::fromMSecsSinceEpoch(1792454400000, QTimeZone::UTC);
  const auto to = from.addDays(15);
  const auto viaHelper = pcm::recurrence::seriesOccurrences(*fixture.db, series, from, to);
  const auto legacy = pcm::recurrence::occurrences(series, from, to);

  ASSERT_EQ(viaHelper.size(), legacy.size());
  for (int i = 0; i < legacy.size(); ++i) {
    EXPECT_EQ(viaHelper[i].toMSecsSinceEpoch(), legacy[i].toMSecsSinceEpoch());
  }
}

TEST(RecurrenceUtilsTest, TimezonePreviewListsTheNearestDatesInThatZone) {
  PinnedSeriesFixture fixture;
  const auto series = fixture.berlinWeekly();
  const auto from = QDateTime::fromMSecsSinceEpoch(1792454400000, QTimeZone::UTC);

  // Moscow has no DST: the stored instant is 19:00 on Tuesdays all autumn.
  const auto moscow = pcm::recurrence::previewOccurrences(series, "Europe/Moscow", from, 3);
  ASSERT_TRUE(moscow.has_value());
  ASSERT_EQ(moscow->size(), 3);
  EXPECT_EQ(utc(moscow->at(0)), "2026-10-20T16:00:00Z");
  EXPECT_EQ(utc(moscow->at(1)), "2026-10-27T16:00:00Z");
  EXPECT_EQ(utc(moscow->at(2)), "2026-11-03T16:00:00Z");

  // In Tokyo the same instant is already Wednesday 01:00, so BYDAY=TU moves the
  // series to the following Tuesday: the preview must show that shift so the
  // specialist can refuse the zone instead of silently moving meetings.
  const auto tokyo = pcm::recurrence::previewOccurrences(series, "Asia/Tokyo", from, 2);
  ASSERT_TRUE(tokyo.has_value());
  EXPECT_EQ(utc(tokyo->at(0)), "2026-10-26T16:00:00Z");

  EXPECT_FALSE(pcm::recurrence::previewOccurrences(series, "Not/AZone", from, 3).has_value());
}

TEST(RecurrenceUtilsTest, VirtualOccurrencesSkipExceptionsAndMaterializedOverrides) {
  PinnedSeriesFixture fixture;
  auto series = fixture.berlinWeekly();
  const auto commit = fixture.db->commit_schedule_change(
      [&]() -> std::optional<int64_t> { return fixture.db->add_event_series(series); },
      "Europe/Berlin", pcm::meeting::buildScheduleSnapshotPayload);
  ASSERT_TRUE(commit.has_value());
  const auto oct27 = 1792512000000 + 7 * 24 * 3'600'000LL + 3'600'000; // 2026-10-27T17:00Z
  ASSERT_TRUE(fixture.db->add_event_series_exception(commit->series_id, oct27, "deleted"));

  const auto from = QDateTime::fromMSecsSinceEpoch(1792454400000, QTimeZone::UTC);
  const auto events = pcm::recurrence::virtualOccurrencesInRange(*fixture.db, from, from.addDays(15));

  ASSERT_EQ(events.size(), 2);
  EXPECT_EQ(events[0].start_date.value(), 1792512000000);
  EXPECT_EQ(events[1].start_date.value(), oct27 + 7 * 24 * 3'600'000LL);
  EXPECT_TRUE(events[0].is_virtual_occurrence);
  EXPECT_EQ(events[0].series_id.value(), commit->series_id);
  EXPECT_EQ(events[0].original_occurrence_start.value(), events[0].start_date.value());
}

TEST(RecurrenceUtilsTest, ScheduleTimezoneSupportIsDecidedByTheSharedModule) {
  EXPECT_TRUE(pcm::recurrence::isSupportedScheduleTimezone("Europe/Berlin"));
  EXPECT_TRUE(pcm::recurrence::isSupportedScheduleTimezone("UTC"));
  EXPECT_FALSE(pcm::recurrence::isSupportedScheduleTimezone(""));
  EXPECT_FALSE(pcm::recurrence::isSupportedScheduleTimezone("localtime"));
  EXPECT_FALSE(pcm::recurrence::isSupportedScheduleTimezone("Not/AZone"));
}

TEST(RecurrenceUtilsTest, DefaultScheduleTimezoneNeverGuessesAnUnknownZone) {
  const auto zone = pcm::recurrence::systemScheduleTimezone();
  // Either the machine's IANA id, accepted by the shared module, or nothing -
  // the caller then asks the specialist instead of publishing a guess.
  EXPECT_TRUE(zone.empty() || pcm::recurrence::isSupportedScheduleTimezone(zone));
}
