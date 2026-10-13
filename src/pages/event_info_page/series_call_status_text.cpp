#include "series_call_status_text.h"

#include <QCoreApplication>

#include <algorithm>

namespace pcm::eventpage {
namespace {

using namespace pcm::meeting;

QString reasonText(const QString &code) {
  if (code == QLatin1String("secret_store_unavailable")) {
    return QCoreApplication::translate("SeriesCallStatus", "the system keychain is not available");
  }
  if (code == QLatin1String("credential_unavailable") ||
      code == QLatin1String("schedule_credential_unavailable")) {
    return QCoreApplication::translate("SeriesCallStatus", "the access key could not be read");
  }
  if (code == QLatin1String("unauthorized") || code == QLatin1String("schedule_unauthorized")) {
    return QCoreApplication::translate("SeriesCallStatus", "the server rejected the access key");
  }
  if (code == QLatin1String("schedule_rejected")) {
    return QCoreApplication::translate("SeriesCallStatus", "the server refused the schedule");
  }
  if (code == QLatin1String("schedule_conflict")) {
    return QCoreApplication::translate("SeriesCallStatus", "the server has a different version of the schedule");
  }
  if (code == QLatin1String("schedule_unsupported")) {
    return QCoreApplication::translate("SeriesCallStatus", "the server does not support recurring calls");
  }
  if (code == QLatin1String("local_state_unavailable")) {
    return QCoreApplication::translate("SeriesCallStatus", "the local database could not be updated");
  }
  return code; // server reason codes: technical, never user data
}

void raise(SeriesStatusView &view, const StatusSeverity severity) {
  if (static_cast<int>(severity) > static_cast<int>(view.severity)) {
    view.severity = severity;
  }
}

} // namespace

SeriesStatusView describeSeriesCallStatus(const SeriesCallStatus &status) {
  SeriesStatusView view;

  switch (status.migration.state) {
  case MigrationState::Publishing:
    view.lines << QCoreApplication::translate("SeriesCallStatus", "Moving this series to a permanent link: sending the schedule to the server...");
    break;
  case MigrationState::CreatingInvitation:
    view.lines << QCoreApplication::translate("SeriesCallStatus", "Moving this series to a permanent link: creating the link...");
    break;
  case MigrationState::Failed:
    view.lines << QCoreApplication::translate("SeriesCallStatus", "Could not move this series to a permanent link (%1). The previous link stays valid.")
                      .arg(reasonText(status.migration.detail));
    view.canRetry = true;
    raise(view, StatusSeverity::Error);
    break;
  case MigrationState::Idle:
  case MigrationState::Completed:
    break;
  }
  if (!status.published) {
    return view;
  }

  const auto &sync = status.sync;
  switch (sync.state) {
  case ScheduleSyncState::Synced:
    view.lines << QCoreApplication::translate("SeriesCallStatus", "Schedule synchronized with the server.");
    break;
  case ScheduleSyncState::Queued:
    view.lines << QCoreApplication::translate("SeriesCallStatus", "Schedule saved on this device, waiting to be sent.");
    break;
  case ScheduleSyncState::Sending:
    view.lines << QCoreApplication::translate("SeriesCallStatus", "Sending the schedule...");
    break;
  case ScheduleSyncState::WaitingForNetwork:
    view.lines << QCoreApplication::translate("SeriesCallStatus", "Waiting for the network. The schedule will be sent automatically.");
    view.canRetry = true;
    raise(view, StatusSeverity::Warning);
    break;
  case ScheduleSyncState::Conflict:
    view.lines << QCoreApplication::translate("SeriesCallStatus", "The server has a different version of this schedule. Nothing was overwritten.");
    view.canPublishThisDevice = true;
    raise(view, StatusSeverity::Error);
    break;
  case ScheduleSyncState::Rejected:
    view.lines << QCoreApplication::translate("SeriesCallStatus", "The server refused this schedule (%1). Your changes are kept on this device.")
                      .arg(reasonText(sync.detail));
    view.canRetry = true;
    raise(view, StatusSeverity::Error);
    break;
  case ScheduleSyncState::Unsupported:
    view.lines << QCoreApplication::translate("SeriesCallStatus", "This server does not support recurring calls yet. The schedule stays on this "
                     "device and no call link can be created.");
    view.canRetry = true;
    raise(view, StatusSeverity::Error);
    break;
  case ScheduleSyncState::Unauthorized:
    view.lines << QCoreApplication::translate("SeriesCallStatus", "The server rejected the saved access key. Check it in Settings.");
    view.canRetry = true;
    raise(view, StatusSeverity::Error);
    break;
  case ScheduleSyncState::CredentialUnavailable:
    view.lines << QCoreApplication::translate("SeriesCallStatus", "The access key could not be read from the system keychain.");
    view.canRetry = true;
    raise(view, StatusSeverity::Warning);
    break;
  }
  if (sync.state != ScheduleSyncState::Synced && sync.unconfirmed && sync.ackedRevision > 0) {
    view.lines << QCoreApplication::translate("SeriesCallStatus", "The server may still allow entry under the previous schedule until this is sent.");
    raise(view, StatusSeverity::Warning);
  }

  const auto &invitation = status.invitation;
  switch (invitation.state) {
  case InvitationState::Ready:
    view.lines << QCoreApplication::translate("SeriesCallStatus", "Permanent link ready.");
    view.linkReady = true;
    // The secret lives in the keychain, not in the database or backups. After a
    // restore or a keychain reset the link can not be read here, and this is
    // the only way to get a usable one again (behind a confirmation).
    view.canReissue = true;
    break;
  case InvitationState::None:
    if (sync.state == ScheduleSyncState::Synced) {
      view.lines << QCoreApplication::translate("SeriesCallStatus", "The permanent link has not been created yet.");
      view.canRetry = true;
    }
    break;
  case InvitationState::WaitingForSchedule:
    view.lines << QCoreApplication::translate("SeriesCallStatus", "The link will be created once the server has the schedule.");
    break;
  case InvitationState::Requesting:
  case InvitationState::StoringSecret:
    view.lines << QCoreApplication::translate("SeriesCallStatus", "Creating the permanent link...");
    break;
  case InvitationState::WaitingForNetwork:
    view.lines << QCoreApplication::translate("SeriesCallStatus", "Waiting for the network to create the link.");
    view.canRetry = true;
    raise(view, StatusSeverity::Warning);
    break;
  case InvitationState::NeedsReissue:
    view.lines << QCoreApplication::translate("SeriesCallStatus", "The link is not available on this device. Create a new link; the old one will "
                     "stop working.");
    view.canReissue = true;
    raise(view, StatusSeverity::Warning);
    break;
  case InvitationState::Failed:
    view.lines << QCoreApplication::translate("SeriesCallStatus", "Could not create the link (%1).").arg(reasonText(invitation.detail));
    view.canRetry = true;
    raise(view, StatusSeverity::Error);
    break;
  }
  return view;
}

} // namespace pcm::eventpage
