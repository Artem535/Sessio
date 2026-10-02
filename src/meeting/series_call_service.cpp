#include "series_call_service.h"

#include "series_join_target.h"

namespace pcm::meeting {

SeriesCallService::SeriesCallService(pcm::database::Database &db, ScheduleSync &sync,
                                     SeriesInvitationService &invitations,
                                     SeriesScheduleCommitter &committer,
                                     MeetingCoordinator *coordinator, QObject *parent)
    : QObject(parent), mDb(db), mSync(sync), mInvitations(invitations), mCommitter(committer),
      mCoordinator(coordinator) {
  qRegisterMetaType<SeriesCallStatus>();
  connect(&mSync, &ScheduleSync::statusChanged, this, [this](const QString &uid) {
    if (const auto id = seriesIdForUid(uid)) {
      emit statusChanged(*id);
    }
  });
  connect(&mInvitations, &SeriesInvitationService::statusChanged, this,
          [this](qint64 seriesId, const InvitationStatus &) {
            onInvitationStatus(seriesId);
            emit statusChanged(seriesId);
          });
}

std::optional<int64_t> SeriesCallService::seriesIdForUid(const QString &uid) {
  if (const auto identity = mDb.get_schedule_identity_by_uid(uid.toStdString())) {
    return identity->series_id;
  }
  return std::nullopt;
}

SeriesCallStatus SeriesCallService::status(const int64_t seriesId) {
  SeriesCallStatus result;
  result.migration = mMigrations.value(seriesId);
  const auto identity = mDb.get_schedule_identity(seriesId);
  if (!identity.has_value()) {
    return result;
  }
  result.published = true;
  result.seriesUid = QString::fromStdString(identity->series_uid);
  result.timezone = QString::fromStdString(identity->timezone);
  result.sync = mSync.status(result.seriesUid);
  result.invitation = mInvitations.status(seriesId);
  return result;
}

bool SeriesCallService::isLegacyLiveKitSeries(const DuckEventSeries &series) {
  return series.provider_kind.value_or("") == "LiveKit" && series.meeting_ref.has_value() &&
         !series.meeting_ref->empty();
}

bool SeriesCallService::isSeriesBacked(const DuckEventSeries &series) {
  return series.id > 0 && series.provider_kind.value_or("") == "LiveKit" &&
         !isLegacyLiveKitSeries(series) && mDb.get_schedule_identity(series.id).has_value();
}

std::optional<QString> SeriesCallService::joinTargetFor(const DuckEvent &event) {
  if (!event.series_id.has_value() || !event.original_occurrence_start.has_value() ||
      event.provider_kind.value_or("") != "LiveKit" ||
      (event.meeting_ref.has_value() && !event.meeting_ref->empty())) {
    return std::nullopt;
  }
  const auto series = mDb.get_event_series(*event.series_id);
  if (!series || !isSeriesBacked(*series)) {
    return std::nullopt;
  }
  const auto identity = mDb.get_schedule_identity(*event.series_id);
  return encodeSeriesJoinTarget(QString::fromStdString(identity->series_uid),
                                *event.original_occurrence_start);
}

void SeriesCallService::ensureInvitation(const int64_t seriesId) {
  mInvitations.ensureInvitation(seriesId);
}

void SeriesCallService::reissueInvitation(const int64_t seriesId) {
  mInvitations.reissueInvitation(seriesId);
}

void SeriesCallService::loadInvitation(const int64_t seriesId,
                                       const SeriesInvitationService::LoadCallback &done) {
  mInvitations.loadInvitation(seriesId, done);
}

void SeriesCallService::retry(const int64_t seriesId) {
  const auto identity = mDb.get_schedule_identity(seriesId);
  if (!identity.has_value()) {
    return;
  }
  const auto uid = QString::fromStdString(identity->series_uid);
  mSync.wake();
  mSync.retry(uid);
  const auto invitation = mInvitations.status(seriesId);
  const bool migrating = mMigrations.value(seriesId).state != MigrationState::Idle &&
                         mMigrations.value(seriesId).state != MigrationState::Completed;
  if (migrating || invitation.state == InvitationState::Failed ||
      invitation.state == InvitationState::WaitingForNetwork ||
      invitation.state == InvitationState::WaitingForSchedule) {
    if (migrating) {
      setMigration(seriesId, MigrationState::Publishing);
    }
    mInvitations.retry(seriesId);
  }
}

void SeriesCallService::setMigration(const int64_t seriesId, const MigrationState state,
                                     const QString &detail) {
  mMigrations.insert(seriesId, {state, detail});
  emit statusChanged(seriesId);
}

void SeriesCallService::migrateLegacySeries(const int64_t seriesId, const QString &timezone) {
  const auto series = mDb.get_event_series(seriesId);
  if (!series || !isLegacyLiveKitSeries(*series)) {
    setMigration(seriesId, MigrationState::Failed, QStringLiteral("not_a_legacy_livekit_series"));
    return;
  }
  if (!mDb.get_schedule_identity(seriesId).has_value()) {
    // Pin the confirmed timezone and queue the schedule. A refusal rolls the
    // whole change back, so the series stays exactly as it was.
    const auto result = mCommitter.commit(
        [seriesId]() -> std::optional<int64_t> { return seriesId; }, timezone.toStdString());
    if (!result.ok) {
      setMigration(seriesId, MigrationState::Failed,
                   result.detail.isEmpty() ? result.error : result.detail);
      return;
    }
  }
  const auto invitation = mInvitations.status(seriesId);
  if (invitation.state == InvitationState::Ready) {
    finishMigration(seriesId); // an earlier attempt got this far; only the cleanup is left
    return;
  }
  setMigration(seriesId, MigrationState::Publishing);
  mInvitations.ensureInvitation(seriesId);
}

void SeriesCallService::onInvitationStatus(const int64_t seriesId) {
  const auto migration = mMigrations.value(seriesId);
  if (migration.state != MigrationState::Publishing &&
      migration.state != MigrationState::CreatingInvitation) {
    return;
  }
  const auto invitation = mInvitations.status(seriesId);
  switch (invitation.state) {
  case InvitationState::Ready:
    finishMigration(seriesId);
    break;
  case InvitationState::Requesting:
  case InvitationState::StoringSecret:
  case InvitationState::WaitingForNetwork:
    setMigration(seriesId, MigrationState::CreatingInvitation, invitation.detail);
    break;
  case InvitationState::WaitingForSchedule:
    if (invitation.detail == QLatin1String("schedule_not_acknowledged")) {
      setMigration(seriesId, MigrationState::Publishing);
    } else {
      // Conflict, rejection, unsupported server or unauthorized: stuck until
      // the specialist acts, and the old meeting must stay as it is.
      setMigration(seriesId, MigrationState::Failed, invitation.detail);
    }
    break;
  case InvitationState::NeedsReissue:
  case InvitationState::Failed:
    setMigration(seriesId, MigrationState::Failed, invitation.detail);
    break;
  case InvitationState::None:
    break;
  }
}

void SeriesCallService::finishMigration(const int64_t seriesId) {
  // The invitation exists and its secret is in secure storage; the schedule
  // is acknowledged. Only now does the old local relationship go.
  const auto refs = mDb.clear_series_legacy_meeting(seriesId);
  if (!refs.has_value()) {
    setMigration(seriesId, MigrationState::Failed, QStringLiteral("local_state_unavailable"));
    return;
  }
  if (mCoordinator) {
    for (const auto &ref : *refs) {
      // Fire-and-forget: the series no longer references these meetings, and a
      // failed invalidate leaves nothing the UI must react to.
      mCoordinator->cancelMeeting(ProviderKind::LiveKit, QString::fromStdString(ref));
    }
  }
  setMigration(seriesId, MigrationState::Completed);
}

} // namespace pcm::meeting
