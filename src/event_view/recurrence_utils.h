#pragma once

#include "schema.hpp"

#include <QDate>
#include <QDateTime>
#include <QPair>
#include <QTime>
#include <QVector>

#include <optional>

namespace pcm::database {
class Database;
}

namespace pcm::recurrence {

QString fullClientName(const DuckClient &client);
void resolveSeriesClientName(pcm::database::Database &db, DuckEventSeries &series);
QString weeklyRuleForDate(const QDate &date, int intervalWeeks = 1);
QVector<QDateTime> occurrences(const DuckEventSeries &series,
                               const QDateTime &rangeStart,
                               const QDateTime &rangeEnd);
// Base occurrence starts of one series. A series with a pinned timezone (a
// published series, see Database::get_schedule_identity) is expanded by the
// shared schedule module in that timezone, so DST and the wall clock follow the
// series rather than the machine; every other series keeps the legacy
// local-calendar calculation until it is explicitly migrated.
QVector<QDateTime> seriesOccurrences(pcm::database::Database &db, const DuckEventSeries &series,
                                     const QDateTime &rangeStart, const QDateTime &rangeEnd);
// Not-yet-materialized occurrences of all active series in the range, without
// exceptions and without occurrences that already have their own Event row.
QVector<DuckEvent> virtualOccurrencesInRange(pcm::database::Database &db,
                                             const QDateTime &rangeStart, const QDateTime &rangeEnd);
// The next `count` occurrence starts at or after `from` if the series were
// pinned to `timezone` (empty: the legacy calendar the series has today). Used
// to show the specialist what a timezone confirmation would do. nullopt: the
// timezone is unknown or the series cannot be expressed in it.
std::optional<QVector<QDateTime>> previewOccurrences(const DuckEventSeries &series,
                                                     const std::string &timezone,
                                                     const QDateTime &from, int count);
// True when the shared schedule module (and therefore the server) accepts this
// IANA timezone id.
bool isSupportedScheduleTimezone(const std::string &timezone);
// The machine's IANA timezone when it is supported, otherwise empty: a series
// is never published with a guessed timezone.
std::string systemScheduleTimezone();
DuckEvent buildVirtualOccurrence(const DuckEventSeries &series,
                                 const QDateTime &occurrenceStart,
                                 int64_t virtualId);

// Decodes a virtual occurrence id (-(series.id*1'000'000 + julianDay)) back to its occurrence.
// Julian days (~2.46M) overflow the stride, so the day is read from [2'000'000, 3'000'000).
// nullopt: not a virtual id, the series is gone or inactive, the occurrence is an exception,
// or the series' rule no longer produces an occurrence on that local day.
std::optional<DuckEvent> virtualOccurrenceForId(pcm::database::Database &db, int64_t virtualId);

// The id of the already materialised event for a virtual occurrence id, found without
// requiring the series to be active or its rule to still produce that day.
std::optional<int64_t> materialisedEventForVirtualId(pcm::database::Database &db,
                                                     int64_t virtualId);

QVector<DuckEvent> eventsForClient(pcm::database::Database &db, int64_t clientId,
                                   const QDateTime &virtualWindowStart,
                                   const QDateTime &virtualWindowEnd);

struct LastNextAppointment {
  std::optional<DuckEvent> last;
  std::optional<DuckEvent> next;
};

LastNextAppointment lastAndNextAppointment(const QVector<DuckEvent> &events, qint64 nowMs);

struct DaySummary {
  QDate date;
  bool hasSessions = false;
  int sessionCount = 0;
  int clientCount = 0;
  qint64 busyMinutes = 0;
  std::optional<DuckEvent> nextSession;
  std::optional<QDateTime> freeWindowStart;
  std::optional<QDateTime> freeWindowEnd;
  QVector<DuckEvent> upcoming; // up to 3, chronological
};

DaySummary computeDaySummary(const QVector<DuckEvent> &events,
                             const QVector<QPair<QDateTime, QDateTime>> &busyIntervals,
                             QTime workDayStart, QTime workDayEnd,
                             const QDate &selectedDate, qint64 nowMs,
                             int minFreeWindowMinutes);

std::optional<DuckEvent> resolveNoteLink(pcm::database::Database &db, const DuckClientNote &note);

} // namespace pcm::recurrence
