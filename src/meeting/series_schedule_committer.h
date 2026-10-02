#pragma once

#include "database.h"
#include "schedule_sync.h"

#include <QObject>
#include <QPointer>
#include <QString>

namespace pcm::meeting {

// Applies one local edit of a recurring series and, for a published series,
// enqueues the resulting full snapshot in the SAME database transaction
// (Database::commit_schedule_change), then wakes the sync service.
//
// The snapshot is validated with the shared schedule module before anything is
// written: an edit the server would refuse (unknown timezone, unsupported
// recurrence) is rolled back as a whole and reported, never half-saved.
class SeriesScheduleCommitter final : public QObject {
  Q_OBJECT

public:
  struct Result {
    bool ok = false;
    int64_t seriesId = 0; // the series the mutation affected
    QString seriesUid; // empty for a legacy series (no outbox entry)
    QString error;     // "invalid_schedule" or "local_failure" when !ok
    QString detail;    // technical reason (never user data)
  };

  explicit SeriesScheduleCommitter(pcm::database::Database &db, QObject *parent = nullptr)
      : QObject(parent), mDb(db) {}

  void setSync(ScheduleSync *sync) { mSync = sync; }

  // `timezone` is only used when the series has no schedule identity yet: a
  // non-empty value publishes it from now on (new series, or an explicit
  // migration); empty keeps a legacy series out of the outbox.
  Result commit(const pcm::database::ScheduleMutation &mutation, const std::string &timezone);

  // True when the series already has a schedule identity (it is published).
  [[nodiscard]] bool isPublished(int64_t seriesId);

private:
  pcm::database::Database &mDb;
  QPointer<ScheduleSync> mSync;
};

} // namespace pcm::meeting
