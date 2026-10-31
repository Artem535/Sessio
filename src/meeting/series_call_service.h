#pragma once

#include "meeting_coordinator.h"
#include "schedule_sync.h"
#include "series_invitation_service.h"
#include "series_schedule_committer.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <optional>

namespace pcm::meeting {

enum class MigrationState {
  Idle,
  Publishing,         // schedule accepted locally; waiting for the server's ACK
  CreatingInvitation, // schedule acknowledged; waiting for the invitation and its storage
  Completed,
  Failed
};

struct MigrationStatus {
  MigrationState state = MigrationState::Idle;
  QString detail; // machine-readable code, never a secret
};

// Everything the UI shows about one recurring call series.
struct SeriesCallStatus {
  bool published = false; // has a schedule identity (server-side series)
  QString seriesUid;
  QString timezone;
  ScheduleSyncStatus sync;
  InvitationStatus invitation;
  MigrationStatus migration;
};

// The UI-facing facade of the recurring-call feature: one object the event
// editor, the timeline and the Calls tab talk to instead of the sync service,
// the invitation service and the migration logic separately.
class SeriesCallService final : public QObject {
  Q_OBJECT

public:
  SeriesCallService(pcm::database::Database &db, ScheduleSync &sync,
                    SeriesInvitationService &invitations, SeriesScheduleCommitter &committer,
                    MeetingCoordinator *coordinator, QObject *parent = nullptr);

  [[nodiscard]] SeriesCallStatus status(int64_t seriesId);

  // Join target (see series_join_target.h) of an occurrence of a published
  // series, addressed by the occurrence's ORIGINAL start; nullopt for single
  // events, legacy series and series still holding their old single meeting.
  [[nodiscard]] std::optional<QString> joinTargetFor(const DuckEvent &event);
  // True for a LiveKit series published to the server with no legacy meeting.
  [[nodiscard]] bool isSeriesBacked(const DuckEventSeries &series);

  // Makes sure a published series has its permanent invitation (no-op when it
  // exists; waits for the schedule ACK first).
  void ensureInvitation(int64_t seriesId);
  void reissueInvitation(int64_t seriesId);
  // Manual retry of everything that is stuck for this series: wakes the
  // schedule queue, repeats a failed invitation request and a failed migration.
  void retry(int64_t seriesId);
  // Explicit "publish this device's schedule" after a conflict: the server's
  // revision is adopted and the local schedule is re-queued on top of it, so the
  // server copy is replaced. Only ever called from a deliberate user action.
  void publishThisDeviceSchedule(int64_t seriesId);
  // Asynchronous read of the stored invitation (link + passcode) from secure
  // storage, for "copy link" / "copy invite".
  void loadInvitation(int64_t seriesId, const SeriesInvitationService::LoadCallback &done);

  // Explicit, opt-in move of a legacy LiveKit series (one shared meeting) to a
  // published series. `timezone` is the IANA zone the specialist confirmed.
  // Order is fixed: publish the schedule -> wait for the server's ACK -> create
  // and securely store the permanent invitation -> only then drop the old local
  // meeting references and invalidate the old backend meetings. Any failure
  // before that leaves the old local relationship untouched.
  void migrateLegacySeries(int64_t seriesId, const QString &timezone);
  [[nodiscard]] static bool isLegacyLiveKitSeries(const DuckEventSeries &series);

signals:
  void statusChanged(qint64 seriesId);
  // The old single meeting of a migrated series could not be invalidated on the
  // server. The migration itself is complete (the series no longer references
  // it) but the old shared link may keep working until it expires.
  void legacyInvalidationFailed(qint64 seriesId);

private:
  void onInvitationStatus(int64_t seriesId);
  void finishMigration(int64_t seriesId);
  void setMigration(int64_t seriesId, MigrationState state, const QString &detail = {});
  [[nodiscard]] std::optional<int64_t> seriesIdForUid(const QString &uid);

  pcm::database::Database &mDb;
  ScheduleSync &mSync;
  SeriesInvitationService &mInvitations;
  SeriesScheduleCommitter &mCommitter;
  QPointer<MeetingCoordinator> mCoordinator;
  QHash<qint64, MigrationStatus> mMigrations;
  // Series whose old meeting invalidation is in flight (FIFO, matched to the
  // coordinator's meetingCanceled / meetingCancelFailed answers).
  QList<qint64> mPendingInvalidations;
};

} // namespace pcm::meeting

Q_DECLARE_METATYPE(pcm::meeting::SeriesCallStatus)
