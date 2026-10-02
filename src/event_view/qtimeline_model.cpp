// src/timeline_widget/timeline_model.cpp
#include "qtimeline_model.h"
#include "provider_kind.h"
#include "recurrence_utils.h"
#include "schedule_conflict_service.h"
#include <QDateTime>
#include <QTimeZone>
#include <algorithm>
#include <set>

QTimelineModel::QTimelineModel(
    const std::shared_ptr<pcm::database::Database> &db,
    pcm::meeting::MeetingCoordinator *meetingCoordinator, QObject *parent)
    : QAbstractItemModel(parent), mDb(db), mMeetingCoordinator(meetingCoordinator) {}

namespace {

void applySeriesFieldsFromEvent(DuckEventSeries &series, const DuckEvent &event,
                                const int64_t clientId, const QString &recurrenceRule,
                                const std::optional<int64_t> recurrenceUntilMs) {
  series.name = event.name;
  series.description = event.description;
  series.client_id = clientId > 0 ? std::make_optional(clientId) : std::nullopt;
  series.is_work_event = event.is_work_event;
  series.event_stat_id = event.event_stat_id;
  series.payment_stat_id = event.payment_stat_id;
  series.cancellation_reason = event.cancellation_reason;
  series.canceled_by = event.canceled_by;
  series.duration = event.duration;
  series.cost = event.cost;
  series.is_online = event.is_online;
  series.meeting_url = event.meeting_url;
  series.provider_kind = event.provider_kind;
  series.meeting_ref = event.meeting_ref;
  series.invitation_state = event.invitation_state;
  series.buffer_before_minutes = event.buffer_before_minutes;
  series.buffer_after_minutes = event.buffer_after_minutes;
  series.recurrence_rule = recurrenceRule.trimmed().toStdString();
  series.recurrence_until = recurrenceUntilMs;
}

void cancelMeetingIfNeeded(pcm::meeting::MeetingCoordinator *coordinator,
                           const DuckEvent &event) {
  // A published-series occurrence is a LiveKit event with no meeting reference
  // of its own (the series owns the room logic); there is nothing to invalidate.
  if (!event.provider_kind.has_value() || !coordinator || !event.meeting_ref.has_value() ||
      event.meeting_ref->empty()) {
    return;
  }
  const auto kind = pcm::meeting::providerKindFromString(*event.provider_kind);
  if (kind.has_value()) {
    coordinator->cancelMeeting(*kind, QString::fromStdString(event.meeting_ref.value_or("")));
  }
}

} // namespace

QModelIndex QTimelineModel::index(int row, int column,
                                  const QModelIndex &parent) const {
  if (!parent.isValid() && row >= 0 && row < mEvents.size() && column == 0)
    return createIndex(row, column);
  return {};
}

QModelIndex QTimelineModel::parent(const QModelIndex &child) const {
  Q_UNUSED(child)
  return {};
}

int QTimelineModel::rowCount(const QModelIndex &parent) const {
  const auto rowCount = parent.isValid() ? 0 : mEvents.size();
  return rowCount;
}

int QTimelineModel::columnCount(const QModelIndex &parent) const {
  Q_UNUSED(parent)
  return 1;
}

QVariant QTimelineModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= mEvents.size())
    return {};

  const auto &event = mEvents.at(index.row());
  switch (role) {
  case IdRole:
    return static_cast<qint64>(event.id);
  case TitleRole:
    return QString::fromStdString(event.name.value_or("Undefined"));
  case DescriptionRole:
    return QString::fromStdString(event.description.value_or("Undefined"));
  case IsWorkRole:
    return event.is_work_event;
  case StartDateTimeRole:
    return QDateTime::fromMSecsSinceEpoch(event.start_date.value_or(0),
                                          QTimeZone::UTC)
        .toLocalTime();
  case EndDateTimeRole:
    return QDateTime::fromMSecsSinceEpoch(event.end_date.value_or(0),
                                          QTimeZone::UTC)
        .toLocalTime();
  case DurationRole:
    return QVariant::fromValue(event.duration);
  case EventDataRole:
    return QVariant::fromValue(event);
  default:
    return {};
  }
}

QHash<int, QByteArray> QTimelineModel::roleNames() const {
  return {{IdRole, "eventId"},
          {TitleRole, "title"},
          {DescriptionRole, "description"},
          {IsWorkRole, "isWork"},
          {StartDateTimeRole, "startDateTime"},
          {EndDateTimeRole, "endDateTime"},
          {DurationRole, "duration"},
          {EventDataRole, "eventData"}};
}

void QTimelineModel::loadEventsForDay(const QDate &date) {
  beginResetModel();
  mEvents.clear();
  mCurrentDate = date;

  const auto localTz = QTimeZone::systemTimeZone();
  const auto dayStartMs =
      QDateTime(date, QTime(0, 0), localTz).toMSecsSinceEpoch();
  const auto dayEndMs =
      QDateTime(date.addDays(1), QTime(0, 0), localTz).toMSecsSinceEpoch() - 1;

  const auto events = mDb->get_day_events(dayStartMs, dayEndMs);
  mEvents = std::move(QVector<DuckEvent>(events.begin(), events.end()));
  for (auto &event : mEvents) {
    if (!event.is_work_event) {
      continue;
    }

    try {
      const auto client = mDb->get_client_by_event(event.id);
      const auto displayName = pcm::recurrence::fullClientName(client);
      if (!displayName.isEmpty()) {
        event.client_name = displayName.toStdString();
      }
    } catch (const std::exception &) {
      event.client_name = std::nullopt;
    }
  }

  const auto rangeStart = QDateTime(date, QTime(0, 0), localTz);
  const auto rangeEnd = QDateTime(date.addDays(1), QTime(0, 0), localTz).addMSecs(-1);
  for (auto &occurrence : pcm::recurrence::virtualOccurrencesInRange(*mDb, rangeStart, rangeEnd)) {
    mEvents.append(std::move(occurrence));
  }

  std::sort(mEvents.begin(), mEvents.end(), [](const DuckEvent &left, const DuckEvent &right) {
    return left.start_date.value_or(0) < right.start_date.value_or(0);
  });
  qDebug() << "QTimelineModel::loadEventsForDay date=" << date
           << "loaded events=" << mEvents.size();

  endResetModel();
  emit eventsLoaded();
}

void QTimelineModel::setScheduleCommitter(pcm::meeting::SeriesScheduleCommitter *committer) {
  mCommitter = committer;
}

bool QTimelineModel::isSeriesPublished(const int64_t seriesId) const {
  return mDb && seriesId > 0 && mDb->get_schedule_identity(seriesId).has_value();
}

bool QTimelineModel::isSeriesSplitBlocked(const int64_t seriesId) const {
  return isSeriesPublished(seriesId);
}

QString QTimelineModel::lastScheduleError() const {
  if (mLastErrorCode.isEmpty()) {
    return {};
  }
  return mLastErrorDetail.isEmpty() ? mLastErrorCode : mLastErrorCode + QLatin1String(": ") + mLastErrorDetail;
}

void QTimelineModel::reportScheduleFailure(const QString &error, const QString &detail) {
  mLastErrorCode = error;
  mLastErrorDetail = detail;
  emit scheduleCommitFailed(error, detail);
}

std::optional<int64_t>
QTimelineModel::commitSeriesWrite(const std::optional<int64_t> seriesId,
                                  const QString &publishTimezone,
                                  const pcm::database::ScheduleMutation &mutation) {
  const bool published = seriesId.has_value() && isSeriesPublished(*seriesId);
  if (!mCommitter || (!published && publishTimezone.isEmpty())) {
    return mutation(); // legacy series: local change only, nothing to publish
  }
  const auto result = mCommitter->commit(mutation, publishTimezone.toStdString());
  if (!result.ok) {
    reportScheduleFailure(result.error, result.detail);
    return std::nullopt;
  }
  return result.seriesId;
}

namespace {

// What the server's copy of a series depends on. Titles, notes and prices do
// not: editing only those must not publish a new revision.
bool seriesScheduleChanged(const DuckEventSeries &before, const DuckEventSeries &after) {
  return before.start_date != after.start_date || before.end_date != after.end_date ||
         before.recurrence_rule != after.recurrence_rule ||
         before.recurrence_until != after.recurrence_until || before.active != after.active ||
         before.event_stat_id != after.event_stat_id;
}

bool occurrenceScheduleChanged(const DuckEvent &before, const DuckEvent &after) {
  return before.start_date != after.start_date || before.end_date != after.end_date ||
         before.event_stat_id != after.event_stat_id;
}

} // namespace

int64_t QTimelineModel::addEvent(const DuckEvent &event, const bool allowOverlap) {
  DuckEvent newEvent = event;
  int64_t newId = 0;
  if (event.series_id.has_value()) {
    // Materializing one occurrence of a series (a move, a cancellation, a
    // single edit): for a published series this is a schedule change.
    const auto committed = commitSeriesWrite(
        event.series_id, {}, [&]() -> std::optional<int64_t> {
          newId = mDb->add_event(event, allowOverlap);
          return newId > 0 ? event.series_id : std::nullopt;
        });
    if (!committed.has_value()) {
      return 0;
    }
    newEvent.id = newId;
  } else {
    newEvent.id = mDb->add_event(event, allowOverlap); // save to DB
  }
  if (newEvent.id <= 0) {
    return 0;
  }

  const int row = mEvents.size();
  beginInsertRows({}, row, row);
  mEvents.append(newEvent);
  endInsertRows();
  return newEvent.id;
}

int64_t QTimelineModel::addEventSeries(const DuckEvent &event, const int64_t clientId,
                                       const QString &recurrenceRule,
                                       const std::optional<int64_t> recurrenceUntilMs,
                                       const QString &publishTimezone) {
  DuckEventSeries series;
  applySeriesFieldsFromEvent(series, event, clientId, recurrenceRule, recurrenceUntilMs);
  series.start_date = event.start_date;
  series.end_date = event.end_date;

  if (!mDb) {
    return 0;
  }
  int64_t newId = 0;
  const auto committed = commitSeriesWrite(std::nullopt, publishTimezone,
                                           [&]() -> std::optional<int64_t> {
                                             newId = mDb->add_event_series(series);
                                             return newId > 0 ? std::optional<int64_t>(newId)
                                                              : std::nullopt;
                                           });
  return committed.has_value() ? newId : 0;
}

bool QTimelineModel::updateEventSeries(const DuckEvent &event, const int64_t seriesId,
                                       const int64_t clientId,
                                       const QString &recurrenceRule,
                                       const std::optional<int64_t> recurrenceUntilMs) {
  if (!mDb || seriesId <= 0) {
    return false;
  }

  DuckEventSeries series;
  std::optional<DuckEventSeries> existingSeriesValue;
  if (const auto existingSeries = mDb->get_event_series(seriesId)) {
    series = *existingSeries;
    existingSeriesValue = *existingSeries;
  }
  series.id = seriesId;
  applySeriesFieldsFromEvent(series, event, clientId, recurrenceRule, recurrenceUntilMs);

  // A published series keeps the wall clock of its own timezone, not of the
  // machine it is edited on.
  auto zone = QTimeZone::systemTimeZone();
  const auto identity = mDb->get_schedule_identity(seriesId);
  if (identity.has_value() && !identity->timezone.empty()) {
    const QTimeZone pinned(QByteArray::fromStdString(identity->timezone));
    if (pinned.isValid()) {
      zone = pinned;
    }
  }

  if (existingSeriesValue.has_value() && existingSeriesValue->start_date.has_value() &&
      event.start_date.has_value() && event.end_date.has_value()) {
    const auto originalStart =
        QDateTime::fromMSecsSinceEpoch(*existingSeriesValue->start_date, zone);
    const auto editedStart = QDateTime::fromMSecsSinceEpoch(*event.start_date, zone);
    const auto updatedStart =
        QDateTime(originalStart.date(), editedStart.time(), zone).toMSecsSinceEpoch();
    const auto durationMs = *event.end_date - *event.start_date;
    series.start_date = updatedStart;
    series.end_date = updatedStart + durationMs;
  } else {
    series.start_date = event.start_date;
    series.end_date = event.end_date;
  }

  const bool publishes = identity.has_value() && existingSeriesValue.has_value() &&
                         seriesScheduleChanged(*existingSeriesValue, series);
  if (identity.has_value() && !publishes) {
    return mDb->update_event_series(series); // title, notes, price...: nothing to publish
  }
  return commitSeriesWrite(seriesId, {}, [&]() -> std::optional<int64_t> {
           return mDb->update_event_series(series) ? std::optional<int64_t>(seriesId) : std::nullopt;
         }).has_value();
}

bool QTimelineModel::deactivateEventSeries(const int64_t seriesId) {
  if (!mDb) {
    return false;
  }
  return commitSeriesWrite(seriesId, {}, [&]() -> std::optional<int64_t> {
           return mDb->deactivate_event_series(seriesId) ? std::optional<int64_t>(seriesId)
                                                         : std::nullopt;
         }).has_value();
}

bool QTimelineModel::removeFutureEventSeriesOccurrences(
    const int64_t seriesId, const int64_t occurrenceStartMs) {
  if (!mDb || seriesId <= 0 || occurrenceStartMs <= 0) {
    return false;
  }
  if (isSeriesSplitBlocked(seriesId)) {
    reportScheduleFailure(QStringLiteral("split_unsupported"), {});
    return false;
  }

  const auto series = mDb->get_event_series(seriesId);
  if (!series) {
    return false;
  }

  auto updatedSeries = *series;
  updatedSeries.recurrence_until = occurrenceStartMs - 1;
  if (!mDb->update_event_series(updatedSeries)) {
    return false;
  }

  return mDb->delete_event_series_overrides_from(seriesId, occurrenceStartMs);
}

std::optional<DuckEventSeries> QTimelineModel::eventSeriesById(const int64_t seriesId) const {
  if (!mDb || seriesId <= 0) {
    return std::nullopt;
  }

  const auto series = mDb->get_event_series(seriesId);
  if (!series) {
    return std::nullopt;
  }
  return *series;
}

void QTimelineModel::removeEvent(int64_t id) {
  for (int i = 0; i < mEvents.size(); ++i) {
    if (mEvents[i].id == id) {
      if (mEvents[i].series_id.has_value() &&
          mEvents[i].original_occurrence_start.has_value()) {
        const auto &occurrence = mEvents[i];
        // The exception and the removal of a materialized occurrence are one
        // change; for a published series they commit with the outbox snapshot.
        const auto committed = commitSeriesWrite(
            occurrence.series_id, {}, [&]() -> std::optional<int64_t> {
              if (!mDb->add_event_series_exception(*occurrence.series_id,
                                                   *occurrence.original_occurrence_start,
                                                   "deleted")) {
                return std::nullopt;
              }
              if (!occurrence.is_virtual_occurrence && !mDb->remove_event(id)) {
                return std::nullopt;
              }
              return occurrence.series_id;
            });
        if (!committed.has_value()) {
          qWarning() << "QTimelineModel::removeEvent failed for recurring occurrence id=" << id;
          return;
        }
        cancelMeetingIfNeeded(mMeetingCoordinator, mEvents[i]);
        beginRemoveRows({}, i, i);
        mEvents.removeAt(i);
        endRemoveRows();
        break;
      }
      cancelMeetingIfNeeded(mMeetingCoordinator, mEvents[i]);
      if (!mDb->remove_event(id)) {
        qWarning() << "QTimelineModel::removeEvent failed for id=" << id;
        return;
      }
      beginRemoveRows({}, i, i);
      mEvents.removeAt(i);
      endRemoveRows();
      break;
    }
  }
}

void QTimelineModel::updateEvent(const DuckEvent &event, const bool allowOverlap) {
  for (int i = 0; i < mEvents.size(); ++i) {
    if (mEvents[i].id == event.id) {
      bool saved = false;
      if (event.series_id.has_value() && isSeriesPublished(*event.series_id)) {
        const auto stored = mDb->get_event(event.id);
        if (stored && !occurrenceScheduleChanged(*stored, event)) {
          saved = mDb->update_event(event, allowOverlap); // not part of the server schedule
        } else {
          saved = commitSeriesWrite(event.series_id, {}, [&]() -> std::optional<int64_t> {
                    return mDb->update_event(event, allowOverlap) ? event.series_id : std::nullopt;
                  }).has_value();
        }
      } else {
        saved = mDb->update_event(event, allowOverlap);
      }
      if (!saved) {
        return;
      }
      mEvents[i] = event;
      emit dataChanged(
          index(i, 0, QModelIndex()), index(i, 0, QModelIndex()),
          {EventDataRole, TitleRole, StartDateTimeRole, EndDateTimeRole});
      break;
    }
  }
}

bool QTimelineModel::hasConflict(const DuckEvent &event) const {
  return findConflict(event).has_value();
}

std::optional<DuckEvent> QTimelineModel::findConflict(const DuckEvent &event) const {
  if (const auto conflict = pcm::schedule::findConflict(event, mEvents);
      conflict.has_value()) {
    return conflict;
  }

  return mDb ? mDb->find_conflict(event) : std::nullopt;
}

const QVector<DuckEvent> &QTimelineModel::events() const {
  return mEvents;
}

QModelIndex QTimelineModel::indexForEventId(int64_t id) const {
  for (int i = 0; i < mEvents.size(); ++i) {
    if (mEvents[i].id == id) {
      return index(i, 0, QModelIndex());
    }
  }
  return {};
}
