// src/timeline_widget/timeline_model.h
#pragma once

#include <QAbstractItemModel>
#include <QDate>
#include <QPointer>
#include <QVector>
#include <memory>
#include <optional>

#include "database.h"
#include "event_item.h" // for DuckEvent
#include "meeting_coordinator.h"
#include "series_schedule_committer.h"

Q_DECLARE_METATYPE(DuckEvent)

class QTimelineModel final : public QAbstractItemModel {
  Q_OBJECT

public:
  enum Roles {
    IdRole = Qt::UserRole + 1,
    TitleRole,
    DescriptionRole,
    IsWorkRole,
    StartDateTimeRole,
    EndDateTimeRole,
    DurationRole,
    EventDataRole
  };

  explicit QTimelineModel(const std::shared_ptr<pcm::database::Database> &db,
                          pcm::meeting::MeetingCoordinator *meetingCoordinator,
                          QObject *parent = nullptr);

  QModelIndex index(int row, int column,
                    const QModelIndex &parent) const override;
  QModelIndex parent(const QModelIndex &child) const override;
  int rowCount(const QModelIndex &parent) const override;
  int columnCount(const QModelIndex &parent) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  // API
  void loadEventsForDay(const QDate &date);
  [[nodiscard]] QDate currentDate() const { return mCurrentDate; }
  // Inclusive local dates. Returns owned copies without changing the day model.
  QVector<DuckEvent> eventsForRange(const QDate &first, const QDate &last) const;
  int64_t addEvent(const DuckEvent &event, bool allowOverlap = true);
  // `publishTimezone` (an IANA id) publishes the new series to the server
  // from now on and pins that timezone to it for good; empty keeps the series
  // a local-only (legacy) series. A series the server would refuse is not
  // saved at all: 0 is returned and scheduleCommitFailed() says why.
  int64_t addEventSeries(const DuckEvent &event, int64_t clientId,
                         const QString &recurrenceRule,
                         std::optional<int64_t> recurrenceUntilMs,
                         const QString &publishTimezone = {});
  bool updateEventSeries(const DuckEvent &event, int64_t seriesId, int64_t clientId,
                         const QString &recurrenceRule,
                         std::optional<int64_t> recurrenceUntilMs);
  bool deactivateEventSeries(int64_t seriesId);
  bool removeFutureEventSeriesOccurrences(int64_t seriesId,
                                          int64_t occurrenceStartMs);
  std::optional<DuckEventSeries> eventSeriesById(int64_t seriesId) const;
  void removeEvent(int64_t id);
  void updateEvent(const DuckEvent &event, bool allowOverlap = true);
  bool hasConflict(const DuckEvent &event) const;
  std::optional<DuckEvent> findConflict(const DuckEvent &event) const;
  const QVector<DuckEvent> &events() const;

  // Published-series support. Every edit of a series that has a schedule
  // identity (and every new series given a timezone) is committed atomically
  // with its outbox snapshot through this committer.
  void setScheduleCommitter(pcm::meeting::SeriesScheduleCommitter *committer);
  [[nodiscard]] bool isSeriesPublished(int64_t seriesId) const;
  // "This and following" would have to move the permanent invitation to a new
  // series; until the server supports that, such a split is refused for a
  // published series.
  [[nodiscard]] bool isSeriesSplitBlocked(int64_t seriesId) const;
  // Code ("invalid_schedule", "local_failure", "split_unsupported") and detail
  // of the most recent refused schedule change; empty when none.
  [[nodiscard]] QString lastScheduleError() const;

  QModelIndex indexForEventId(int64_t id) const;

signals:
  void eventsLoaded();
  // A schedule change was rolled back as a whole (nothing was saved).
  void scheduleCommitFailed(const QString &error, const QString &detail);

private:
  QVector<DuckEvent> projectEvents(const QDate &first, const QDate &last,
                                  bool includeOverlappingOccurrences) const;
  // Runs a series write through the committer when the series is published;
  // `plain` is used otherwise. Returns the series id or nullopt (failure
  // already reported).
  std::optional<int64_t> commitSeriesWrite(std::optional<int64_t> seriesId,
                                           const QString &publishTimezone,
                                           const pcm::database::ScheduleMutation &mutation);
  void reportScheduleFailure(const QString &error, const QString &detail);

  std::shared_ptr<pcm::database::Database> mDb;
  QPointer<pcm::meeting::SeriesScheduleCommitter> mCommitter;
  QString mLastErrorCode;
  QString mLastErrorDetail;
  QPointer<pcm::meeting::MeetingCoordinator> mMeetingCoordinator;
  QVector<DuckEvent> mEvents;
  QDate mCurrentDate;
};
