#include "recurrence_utils.h"

#include "database.h"
#include "schedule/schedule.h"
#include "schedule_snapshot.h"

#include <libical/ical.h>

#include <QSet>
#include <QTimeZone>
#include <algorithm>
#include <cstdlib>

namespace {

QString weekdayToken(const int dayOfWeek) {
  switch (dayOfWeek) {
  case 1:
    return QStringLiteral("MO");
  case 2:
    return QStringLiteral("TU");
  case 3:
    return QStringLiteral("WE");
  case 4:
    return QStringLiteral("TH");
  case 5:
    return QStringLiteral("FR");
  case 6:
    return QStringLiteral("SA");
  case 7:
  default:
    return QStringLiteral("SU");
  }
}

icaltimetype toIcalFloatingTime(const QDateTime &dateTime) {
  const auto local = dateTime.toLocalTime();
  return icaltime_from_string(local.toString(QStringLiteral("yyyyMMdd'T'HHmmss"))
                                  .toLatin1()
                                  .constData());
}

QDateTime fromIcalFloatingTime(const icaltimetype &time) {
  return QDateTime(QDate(time.year, time.month, time.day),
                   QTime(time.hour, time.minute, time.second),
                   QTimeZone::systemTimeZone());
}

} // namespace

namespace pcm::recurrence {

QString fullClientName(const DuckClient &client) {
  const auto firstName = QString::fromStdString(client.name.value_or(""));
  const auto lastName = QString::fromStdString(client.last_name.value_or(""));
  return QString("%1 %2").arg(firstName, lastName).trimmed();
}

void resolveSeriesClientName(pcm::database::Database &db, DuckEventSeries &series) {
  if (!series.is_work_event || !series.client_id.has_value()) {
    return;
  }
  try {
    const auto client = db.get_client(*series.client_id);
    if (client) {
      const auto fullName = fullClientName(*client);
      if (!fullName.isEmpty()) {
        series.client_name = fullName.toStdString();
      }
    }
  } catch (const std::exception &) {
    series.client_name = std::nullopt;
  }
}

QString weeklyRuleForDate(const QDate &date, const int intervalWeeks) {
  return QStringLiteral("FREQ=WEEKLY;INTERVAL=%1;BYDAY=%2")
      .arg(std::max(1, intervalWeeks))
      .arg(weekdayToken(date.dayOfWeek()));
}

QVector<QDateTime> occurrences(const DuckEventSeries &series,
                               const QDateTime &rangeStart,
                               const QDateTime &rangeEnd) {
  QVector<QDateTime> occurrences;
  if (!series.start_date.has_value() || series.recurrence_rule.empty() ||
      !rangeStart.isValid() || !rangeEnd.isValid() || rangeEnd < rangeStart) {
    return occurrences;
  }

  const auto localTz = QTimeZone::systemTimeZone();
  const auto seriesStart =
      QDateTime::fromMSecsSinceEpoch(*series.start_date, QTimeZone::UTC).toTimeZone(localTz);
  if (!seriesStart.isValid()) {
    return occurrences;
  }

  if (series.recurrence_until.has_value() &&
      QDateTime::fromMSecsSinceEpoch(*series.recurrence_until, QTimeZone::UTC)
              .toTimeZone(localTz) < rangeStart) {
    return occurrences;
  }

  auto rule = icalrecurrencetype_from_string(series.recurrence_rule.c_str());
  if (rule.freq == ICAL_NO_RECURRENCE) {
    std::free(rule.rscale);
    return occurrences;
  }

  auto *iterator = icalrecur_iterator_new(rule, toIcalFloatingTime(seriesStart));
  if (!iterator) {
    std::free(rule.rscale);
    return occurrences;
  }

  constexpr int kMaxOccurrencesPerRange = 10000;
  for (int i = 0; i < kMaxOccurrencesPerRange; ++i) {
    const auto next = icalrecur_iterator_next(iterator);
    if (icaltime_is_null_time(next)) {
      break;
    }

    const auto occurrence = fromIcalFloatingTime(next);
    if (!occurrence.isValid()) {
      continue;
    }
    if (series.recurrence_until.has_value() &&
        occurrence.toUTC().toMSecsSinceEpoch() > *series.recurrence_until) {
      break;
    }
    if (occurrence > rangeEnd) {
      break;
    }
    if (occurrence >= rangeStart) {
      occurrences.append(occurrence);
    }
  }

  icalrecur_iterator_free(iterator);
  std::free(rule.rscale);
  return occurrences;
}

namespace {

std::optional<QVector<QDateTime>> occurrencesInTimezone(const DuckEventSeries &series,
                                                        const std::string &timezone,
                                                        const QDateTime &rangeStart,
                                                        const QDateTime &rangeEnd) {
  pcm::database::ScheduleSource source;
  source.series = series;
  source.identity.timezone = timezone;
  auto snapshot = pcm::meeting::buildScheduleSnapshot(source);
  if (!snapshot.has_value() || !rangeStart.isValid() || !rangeEnd.isValid() ||
      rangeEnd < rangeStart) {
    return std::nullopt;
  }
  snapshot->revision = 1; // only the recurrence is evaluated; the schedule module wants a valid pair
  snapshot->baseRevision = 0;
  const auto resolution = pcm::schedule::occurrencesBetween(
      *snapshot, rangeStart.toUTC().toMSecsSinceEpoch(), rangeEnd.toUTC().toMSecsSinceEpoch());
  if (resolution.status != pcm::schedule::Status::Available &&
      resolution.status != pcm::schedule::Status::Unavailable) {
    return std::nullopt;
  }
  QVector<QDateTime> result;
  result.reserve(static_cast<qsizetype>(resolution.occurrences.size()));
  for (const auto &occurrence : resolution.occurrences) {
    result.append(QDateTime::fromMSecsSinceEpoch(occurrence.startMs, QTimeZone::UTC)
                      .toTimeZone(QTimeZone::systemTimeZone()));
  }
  return result;
}

} // namespace

QVector<QDateTime> seriesOccurrences(pcm::database::Database &db, const DuckEventSeries &series,
                                     const QDateTime &rangeStart, const QDateTime &rangeEnd) {
  if (series.id > 0) {
    if (const auto identity = db.get_schedule_identity(series.id);
        identity.has_value() && !identity->timezone.empty()) {
      if (auto pinned = occurrencesInTimezone(series, identity->timezone, rangeStart, rangeEnd)) {
        return *pinned;
      }
      qWarning() << "Pinned-timezone recurrence could not be evaluated for series" << series.id
                 << "; showing the local calendar instead";
    }
  }
  return occurrences(series, rangeStart, rangeEnd);
}

QVector<DuckEvent> virtualOccurrencesInRange(pcm::database::Database &db,
                                             const QDateTime &rangeStart,
                                             const QDateTime &rangeEnd) {
  QVector<DuckEvent> result;
  const auto startMs = rangeStart.toUTC().toMSecsSinceEpoch();
  const auto endMs = rangeEnd.toUTC().toMSecsSinceEpoch();
  const auto exceptions = db.get_event_series_exceptions_for_range(startMs, endMs);
  auto seriesList = db.get_event_series_for_range(startMs, endMs);
  for (auto &series : seriesList) {
    resolveSeriesClientName(db, series);
    const auto materializedStarts = db.get_materialized_occurrence_starts_for_series(series.id);
    for (const auto &occurrence : seriesOccurrences(db, series, rangeStart, rangeEnd)) {
      const auto occurrenceStartMs = occurrence.toUTC().toMSecsSinceEpoch();
      if (exceptions.contains({series.id, occurrenceStartMs}) ||
          materializedStarts.contains(occurrenceStartMs)) {
        continue;
      }
      const auto virtualId =
          -(series.id * 1'000'000LL + static_cast<int64_t>(occurrence.date().toJulianDay()));
      result.append(buildVirtualOccurrence(series, occurrence, virtualId));
    }
  }
  std::sort(result.begin(), result.end(), [](const DuckEvent &left, const DuckEvent &right) {
    return left.start_date.value_or(0) < right.start_date.value_or(0);
  });
  return result;
}

bool isSupportedScheduleTimezone(const std::string &timezone) {
  if (timezone.empty()) {
    return false;
  }
  pcm::schedule::Snapshot probe;
  probe.revision = 1;
  probe.baseRevision = 0;
  probe.timezone = timezone;
  probe.dtstartLocal = "2026-01-05T09:00:00";
  probe.durationSeconds = 3600;
  probe.rrule = "FREQ=DAILY";
  probe.active = true;
  probe.joinEnabled = true;
  return pcm::schedule::validate(probe).valid;
}

std::string systemScheduleTimezone() {
  const auto id = QTimeZone::systemTimeZoneId().toStdString();
  return isSupportedScheduleTimezone(id) ? id : std::string{};
}

std::optional<QVector<QDateTime>> previewOccurrences(const DuckEventSeries &series,
                                                     const std::string &timezone,
                                                     const QDateTime &from, const int count) {
  if (count <= 0 || !from.isValid()) {
    return QVector<QDateTime>{};
  }
  const auto to = from.addYears(3);
  std::optional<QVector<QDateTime>> all;
  if (timezone.empty()) {
    all = occurrences(series, from, to);
  } else {
    all = occurrencesInTimezone(series, timezone, from, to);
  }
  if (!all.has_value()) {
    return std::nullopt;
  }
  if (all->size() > count) {
    all->resize(count);
  }
  return all;
}

DuckEvent buildVirtualOccurrence(const DuckEventSeries &series,
                                 const QDateTime &occurrenceStart,
                                 const int64_t virtualId) {
  DuckEvent event;
  event.id = virtualId;
  event.name = series.name;
  event.description = series.description;
  event.client_name = series.client_name;
  event.is_work_event = series.is_work_event;
  event.event_stat_id = series.event_stat_id;
  event.payment_stat_id = series.payment_stat_id;
  event.cancellation_reason = series.cancellation_reason;
  event.canceled_by = series.canceled_by;
  event.start_date = occurrenceStart.toUTC().toMSecsSinceEpoch();
  const auto durationMs =
      series.duration.value_or(3600) * 1000;
  event.end_date = *event.start_date + durationMs;
  event.duration = series.duration;
  event.cost = series.cost;
  event.is_online = series.is_online;
  event.meeting_url = series.meeting_url;
  event.provider_kind = series.provider_kind;
  event.meeting_ref = series.meeting_ref;
  event.invitation_state = series.invitation_state;
  event.buffer_before_minutes = series.buffer_before_minutes;
  event.buffer_after_minutes = series.buffer_after_minutes;
  event.series_id = series.id;
  event.original_occurrence_start = event.start_date;
  event.is_virtual_occurrence = true;
  return event;
}

QVector<DuckEvent> eventsForClient(pcm::database::Database &db, const int64_t clientId,
                                   const QDateTime &virtualWindowStart,
                                   const QDateTime &virtualWindowEnd) {
  QVector<DuckEvent> result;

  const auto materialized = db.get_events_for_client(clientId);
  for (const auto &event : materialized) {
    result.append(event);
  }

  if (!virtualWindowStart.isValid() || !virtualWindowEnd.isValid()) {
    std::sort(result.begin(), result.end(), [](const DuckEvent &left, const DuckEvent &right) {
      return left.start_date.value_or(0) < right.start_date.value_or(0);
    });
    return result;
  }

  const auto windowStartMs = virtualWindowStart.toUTC().toMSecsSinceEpoch();
  const auto windowEndMs = virtualWindowEnd.toUTC().toMSecsSinceEpoch();
  const auto exceptions = db.get_event_series_exceptions_for_range(windowStartMs, windowEndMs);
  auto seriesList =
      db.get_event_series_for_client_and_range(clientId, windowStartMs, windowEndMs);
  for (const auto &series : seriesList) {
    const auto materializedStarts = db.get_materialized_occurrence_starts_for_series(series.id);
    const auto occurrenceList = seriesOccurrences(db, series, virtualWindowStart, virtualWindowEnd);
    for (const auto &occurrence : occurrenceList) {
      const auto occurrenceStartMs = occurrence.toUTC().toMSecsSinceEpoch();
      if (exceptions.contains({series.id, occurrenceStartMs}) ||
          materializedStarts.contains(occurrenceStartMs)) {
        continue;
      }
      const auto virtualId =
          -(series.id * 1'000'000LL + static_cast<int64_t>(occurrence.date().toJulianDay()));
      result.append(buildVirtualOccurrence(series, occurrence, virtualId));
    }
  }

  std::sort(result.begin(), result.end(), [](const DuckEvent &left, const DuckEvent &right) {
    return left.start_date.value_or(0) < right.start_date.value_or(0);
  });

  return result;
}

LastNextAppointment lastAndNextAppointment(const QVector<DuckEvent> &events, const qint64 nowMs) {
  LastNextAppointment result;

  for (const auto &event : events) {
    if (event.event_stat_id != 1 && event.event_stat_id != 2 && event.event_stat_id != 4) {
      continue; // only Scheduled/Completed/Confirmed count as "the appointment"
    }
    if (!event.start_date.has_value()) {
      continue;
    }

    if (*event.start_date < nowMs) {
      if (!result.last.has_value() || *event.start_date > *result.last->start_date) {
        result.last = event;
      }
    } else {
      if (!result.next.has_value() || *event.start_date < *result.next->start_date) {
        result.next = event;
      }
    }
  }

  return result;
}

DaySummary computeDaySummary(const QVector<DuckEvent> &events,
                             const QVector<QPair<QDateTime, QDateTime>> &busyIntervals,
                             const QTime workDayStart, const QTime workDayEnd,
                             const QDate &selectedDate, const qint64 nowMs,
                             const int minFreeWindowMinutes) {
  DaySummary result;
  result.date = selectedDate;

  QVector<DuckEvent> qualifying;
  QSet<QString> namedClients;
  int unnamedClientCount = 0;
  for (const auto &event : events) {
    if (!event.is_work_event) {
      continue;
    }
    if (event.event_stat_id != 1 && event.event_stat_id != 2 && event.event_stat_id != 4) {
      continue; // only Scheduled/Completed/Confirmed count, same as lastAndNextAppointment
    }
    qualifying.append(event);

    const auto clientName = QString::fromStdString(event.client_name.value_or(""));
    if (!clientName.isEmpty()) {
      namedClients.insert(clientName);
    } else {
      ++unnamedClientCount;
    }

    if (event.start_date.has_value() && event.end_date.has_value() &&
        *event.end_date > *event.start_date) {
      result.busyMinutes += (*event.end_date - *event.start_date) / 60'000;
    }
  }

  result.sessionCount = static_cast<int>(qualifying.size());
  result.hasSessions = result.sessionCount > 0;
  result.clientCount = static_cast<int>(namedClients.size()) + unnamedClientCount;

  if (!result.hasSessions) {
    return result;
  }

  std::sort(qualifying.begin(), qualifying.end(),
            [](const DuckEvent &left, const DuckEvent &right) {
              return left.start_date.value_or(0) < right.start_date.value_or(0);
            });

  const bool validWorkHours = workDayStart.isValid() && workDayEnd.isValid() &&
                              workDayStart < workDayEnd;
  const auto dayEnd = QDateTime(selectedDate, workDayEnd, QTimeZone::systemTimeZone());
  const bool showFutureInfo = validWorkHours && nowMs < dayEnd.toMSecsSinceEpoch();

  if (!showFutureInfo) {
    for (const auto &event : qualifying) {
      result.upcoming.append(event);
      if (result.upcoming.size() >= 3) {
        break;
      }
    }
    return result;
  }

  for (const auto &event : qualifying) {
    if (event.start_date.has_value() && *event.start_date > nowMs) {
      result.upcoming.append(event);
      if (result.upcoming.size() >= 3) {
        break;
      }
    }
  }

  for (const auto &event : qualifying) {
    if (event.start_date.has_value() && *event.start_date > nowMs) {
      result.nextSession = event;
      break;
    }
  }

  const auto dayStart = QDateTime(selectedDate, workDayStart, QTimeZone::systemTimeZone());
  const auto now = QDateTime::fromMSecsSinceEpoch(nowMs, QTimeZone::systemTimeZone());

  auto sortedBusy = busyIntervals;
  std::sort(sortedBusy.begin(), sortedBusy.end(),
            [](const QPair<QDateTime, QDateTime> &left, const QPair<QDateTime, QDateTime> &right) {
              return left.first < right.first;
            });

  auto cursor = std::max(dayStart, now);
  for (const auto &interval : sortedBusy) {
    if (cursor >= dayEnd) {
      break;
    }
    if (interval.second <= cursor) {
      continue;
    }
    if (interval.first > cursor) {
      const auto gapEnd = std::min(interval.first, dayEnd);
      if (cursor.secsTo(gapEnd) / 60 >= minFreeWindowMinutes) {
        result.freeWindowStart = cursor;
        result.freeWindowEnd = gapEnd;
        break;
      }
    }
    cursor = std::max(cursor, interval.second);
  }
  if (!result.freeWindowStart.has_value() && cursor < dayEnd &&
      cursor.secsTo(dayEnd) / 60 >= minFreeWindowMinutes) {
    result.freeWindowStart = cursor;
    result.freeWindowEnd = dayEnd;
  }

  return result;
}

std::optional<DuckEvent> resolveNoteLink(pcm::database::Database &db, const DuckClientNote &note) {
  if (note.linked_event_id.has_value()) {
    auto event = db.get_event(*note.linked_event_id);
    if (!event) {
      return std::nullopt;
    }
    return *event;
  }

  if (!note.linked_series_id.has_value() || !note.linked_occurrence_start_ms.has_value()) {
    return std::nullopt;
  }

  auto materialized =
      db.get_event_by_series_occurrence(*note.linked_series_id, *note.linked_occurrence_start_ms);
  if (materialized) {
    return *materialized;
  }

  auto series = db.get_event_series(*note.linked_series_id);
  if (!series) {
    return std::nullopt;
  }

  const auto occurrenceStart =
      QDateTime::fromMSecsSinceEpoch(*note.linked_occurrence_start_ms, QTimeZone::UTC)
          .toTimeZone(QTimeZone::systemTimeZone());
  const auto virtualId =
      -(series->id * 1'000'000LL + static_cast<int64_t>(occurrenceStart.date().toJulianDay()));
  return buildVirtualOccurrence(*series, occurrenceStart, virtualId);
}

} // namespace pcm::recurrence
