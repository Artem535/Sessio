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

  // Constructed first, before any other setup, so a second launch can be
  // detected and forwarded as cheaply as possible.
  std::unique_ptr<SingleInstanceGuard> mSingleInstanceGuard;

  // Shared by both role flows. Declared before the windows so they outlive
  // the CallsPage instances that hold raw pointers to them (members are
  // destroyed in reverse declaration order).
  QString mTokenBackendBaseUrl; // from Config, resolved once in run()
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
  bool mIsQuitting = false;
  bool mTrayCloseHintShown = false;
  config::Config mConf;

  void connectSignals();
};

} // namespace pcm
