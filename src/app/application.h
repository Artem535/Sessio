#pragma once

#include <QApplication>
#include <QEvent>
#include <QObject>
#include <QList>
#include <QLoggingCategory>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QTimer>

#include <memory>

#include "app_role.h"
#include "app_role_switcher.h"
#include "application_restarter.h"
#include "auto_backup_scheduler.h"
#include "app_lock_controller.h"
#include "app_lock_service.h"
#include "call_entry_widget.h"
#include "client_mode_window.h"
#include "client_notes_page.h"
#include "config.h"
#include "database.h"
#include "device_manager.h"
#include "main_window.h"
#include "qclient_model.h"
#include "event_info.h"
#include "keychain_series_invitation_store.h"
#include "schedule_sync.h"
#include "series_call_service.h"
#include "series_invitation_service.h"
#include "series_schedule_committer.h"
#include "store_credential_reader.h"
#include "meeting_coordinator.h"
#include "sessio_url.h"
#include "single_instance_guard.h"
#include "token_backend_client.h"
#include "token_backend_credential_store.h"

class QAction;
class QWidget;

namespace pcm {

class Application final : public QObject {
  Q_OBJECT

public:
  Application();
  int run(int argc, char *argv[], const QString &launchUrl = QString());
  bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
  void saveClient(const DuckClient &client);
  void removeClient(int64_t clientId);
  void fillClientComboBox(QComboBox *box);
  void saveClientEventPair(const int64_t clientId, const int64_t eventId);
  void checkUpcomingEventNotifications();
  void restoreMainWindow();
  void quitApplication();
  void onSettingsSaved();

private:
  QString notificationKey(const DuckEvent &event) const;
  QString notificationTitleForEvent(const DuckEvent &event) const;
  QString notificationBodyForEvent(const DuckEvent &event) const;
  void initializeNotifications();
  void initializeAppLock();
  void checkAppLock();
  void lockApplication();
  void notifyUpcomingSeriesOccurrences(int64_t nowMs, int64_t windowEndMs);
  void restorePendingBackup();
  int runSpecialistFlow(QApplication &app, const QString &launchUrl);
  int runClientFlow(QApplication &app, const QString &launchUrl);
  void loadBearerCredential();
  void refreshUpcomingMeetings();
  void handleJoinLink(const QString &url);
  // Points mTokenClient (and mTokenBackendBaseUrl) at a new token backend.
  void applyTokenBackendBaseUrl(const QString &baseUrl);
  // Writes the token backend URL to Config; logs and skips on read/save error.
  void persistTokenBackendBaseUrl(const QString &baseUrl);

  // Constructed first, before any other setup, so a second launch can be
  // detected and forwarded as cheaply as possible.
  std::unique_ptr<SingleInstanceGuard> mSingleInstanceGuard;
  // Persists a new role and relaunches the executable (releasing
  // mSingleInstanceGuard first, see ApplicationRestarter). Handed to the
  // client window's menu action and the specialist Settings dialog.
  std::unique_ptr<AppRoleSwitcher> mRoleSwitcher;

  // Shared by both role flows. Declared before the windows so they outlive
  // the CallsPage instances that hold raw pointers to them (members are
  // destroyed in reverse declaration order).
  // From Config in run(); updated by a sessio:// link carrying a `backend`
  // item and after the specialist's Settings dialog closes.
  QString mTokenBackendBaseUrl;
  // A sessio:// URL captured by eventFilter()'s QEvent::FileOpen case before
  // mMainWindow/mClientModeWindow exists yet (a macOS cold start via a
  // sessio:// link, delivered while run() is still constructing the app —
  // e.g. during the first-launch role dialog's nested event loop). Replayed
  // via handleJoinLink() and cleared as soon as the relevant window is
  // constructed in runClientFlow()/runSpecialistFlow(), mirroring how the
  // launchUrl parameter is replayed there.
  QString mPendingJoinUrl;
  std::unique_ptr<pcm::video::DeviceManager> mDeviceManager;
  std::unique_ptr<pcm::tokenclient::TokenBackendClient> mTokenClient;
  // Specialist flow only: Client mode never reads the specialist bearer
  // credential, so it never touches the keychain.
  std::unique_ptr<TokenBackendCredentialStore> mTokenCredentialStore;
  QString mBearerCredential; // cached in memory after the async keychain read
  bool mBearerCredentialReadDone = false;

  // Client flow only.
  std::unique_ptr<ClientModeWindow> mClientModeWindow;

  // Specialist flow only.
  QList<UpcomingMeeting> mUpcomingMeetings;
  QPointer<ClientNotesPage> mCallNotesPanel;

  std::unique_ptr<MainWindow> mMainWindow;
  std::shared_ptr<database::Database> mDb;
  std::shared_ptr<QClientModel> mClientModel;
  std::unique_ptr<QSystemTrayIcon> mTrayIcon;
  QAction *mLockAppAction = nullptr;
  QTimer mNotificationTimer;
  QTimer mAppLockTimer;
  std::unique_ptr<AppLockService> mAppLockService;
  std::unique_ptr<AppLockController> mAppLockController;
  QWidget *mAppLockOverlay = nullptr;
  bool mAppLockDialogVisible = false;
  std::unique_ptr<pcm::backup::AutoBackupScheduler> mAutoBackupScheduler;
  std::unique_ptr<pcm::meeting::MeetingCoordinator> mMeetingCoordinator;
  // Recurring-call publishing (specialist flow). Declared last so they are
  // destroyed before the database, token client and credential store they use.
  std::unique_ptr<StoreCredentialReader> mScheduleCredentialReader;
  std::unique_ptr<QtKeychainSeriesInvitationStore> mInvitationSecretStore;
  std::unique_ptr<pcm::meeting::ScheduleSync> mScheduleSync;
  std::unique_ptr<pcm::meeting::SeriesInvitationService> mSeriesInvitations;
  std::unique_ptr<pcm::meeting::SeriesScheduleCommitter> mScheduleCommitter;
  std::unique_ptr<pcm::meeting::SeriesCallService> mSeriesCalls;
  bool mIsQuitting = false;
  bool mTrayCloseHintShown = false;
  config::Config mConf;

  void connectSignals();
};

} // namespace pcm
