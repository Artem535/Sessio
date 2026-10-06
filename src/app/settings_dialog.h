#pragma once

#include "backup_service.h"
#include "credential_store.h"
#include "config.h"
#include "app_lock_service.h"
#include "token_backend_credential_store.h"

#include <QDialog>

#include <memory>
#include <optional>

namespace pcm::database {
class Database;
}

namespace pcm {
class AppRoleSwitcher;
}

class QDialogButtonBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTextEdit;
class QTimeEdit;
class QWidget;

namespace oclero::qlementine {
class ColorEditor;
class SegmentedControl;
class Switch;
}

class SettingsDialog final : public QDialog {
  Q_OBJECT

public:
  explicit SettingsDialog(std::shared_ptr<pcm::database::Database> db,
                          QWidget *parent = nullptr);
  explicit SettingsDialog(std::shared_ptr<pcm::database::Database> db,
                          TokenBackendCredentialStore *tokenBackendCredentialStore,
                          QWidget *parent = nullptr);
  ~SettingsDialog() override = default;

  // Dialog height for a screen with the given available height: leaves room
  // for the window frame and panels, but never exceeds the screen itself.
  static int heightForAvailableScreen(int availableHeight);

  // Not owned. Enables the "Switch to client mode" button; without a switcher
  // it stays disabled.
  void setRoleSwitcher(const pcm::AppRoleSwitcher *switcher);

private:
  void setupUi();
  void setupLiveKitSection();
  void loadSettings() const;
  void connectSignals();
  void openDatabaseFolder() const;
  void createBackup();
  void startBackupWorker(
      const QString &destinationPath,
      std::optional<pcm::backup::BackupEncryptionOptions> encryption = std::nullopt);
  void onManualBackupKeyRead(bool ok, pcm::backup::MasterKey key,
                             const QString &error);
  void enableBackupEncryption();
  void onBackupEncryptionKeyWritten(bool ok, const QString &error);
  void updateBackupEncryptionUi() const;
  void validateBackup();
  void restoreBackup();
  void browseAutoBackupDestination();
  void configureAppLock();
  void saveLiveKitSettings();
  // Never store or surface `credential` itself -- only whether one exists,
  // so mLiveKitBearerCredentialStatusLabel never leaks the actual secret.
  void onBearerCredentialRead(bool ok, const QString &credential, const QString &error);

  oclero::qlementine::SegmentedControl *mSettingsSections{nullptr};
  QStackedWidget *mSettingsStack{nullptr};
  QComboBox *mLanguageCombo{nullptr};
  QLabel *mDatabasePathLabel{nullptr};
  QPushButton *mOpenDatabaseFolderButton{nullptr};
  QPushButton *mSwitchToClientModeButton{nullptr};
  const pcm::AppRoleSwitcher *mRoleSwitcher{nullptr};
  QPushButton *mCreateBackupButton{nullptr};
  QPushButton *mValidateBackupButton{nullptr};
  QPushButton *mRestoreBackupButton{nullptr};
  QProgressBar *mBackupProgressBar{nullptr};
  QLabel *mBackupStatusLabel{nullptr};
  oclero::qlementine::Switch *mBackupEncryptionEnabledSwitch{nullptr};
  QWidget *mBackupEncryptionDetails{nullptr};
  QLabel *mBackupEncryptionWarningLabel{nullptr};
  QLineEdit *mBackupEncryptionPasswordEdit{nullptr};
  QLineEdit *mBackupEncryptionConfirmationEdit{nullptr};
  QPushButton *mEnableBackupEncryptionButton{nullptr};
  oclero::qlementine::Switch *mAutoBackupEnabledSwitch{nullptr};
  QSpinBox *mAutoBackupIntervalSpinBox{nullptr};
  QSpinBox *mAutoBackupKeepCountSpinBox{nullptr};
  QLineEdit *mAutoBackupDestinationEdit{nullptr};
  QPushButton *mAutoBackupBrowseButton{nullptr};
  oclero::qlementine::Switch *mNotificationsEnabledSwitch{nullptr};
  QSpinBox *mNotificationLeadMinutesSpinBox{nullptr};
  QComboBox *mNotificationPrivacyModeCombo{nullptr};
  oclero::qlementine::Switch *mAppLockEnabledSwitch{nullptr};
  QSpinBox *mAppLockTimeoutSpinBox{nullptr};
  QPushButton *mChangeAppLockCredentialButton{nullptr};
  oclero::qlementine::Switch *mClearSensitiveClipboardSwitch{nullptr};
  QSpinBox *mSensitiveClipboardDelaySpinBox{nullptr};
  oclero::qlementine::Switch *mPreventOverlapsSwitch{nullptr};
  oclero::qlementine::ColorEditor *mWorkEventColorEditor{nullptr};
  oclero::qlementine::ColorEditor *mPersonalEventColorEditor{nullptr};
  QComboBox *mCurrencyCombo{nullptr};
  QDoubleSpinBox *mDefaultWorkCostSpinBox{nullptr};
  QTimeEdit *mWorkDayStartEdit{nullptr};
  QTimeEdit *mWorkDayEndEdit{nullptr};
  QSpinBox *mDefaultSessionDurationSpinBox{nullptr};
  QSpinBox *mDefaultBufferBeforeSpinBox{nullptr};
  QSpinBox *mDefaultBufferAfterSpinBox{nullptr};
  QTextEdit *mMeetingInviteTemplateEdit{nullptr};
  QLineEdit *mLiveKitBaseUrlEdit{nullptr};
  QLineEdit *mLiveKitBearerCredentialEdit{nullptr};
  // Reports whether a bearer credential is currently stored in the keychain
  // -- never the credential value itself. Updated on construction (an
  // eager readBearerCredential()) and again after every successful write,
  // so the field's write-only design ("leave blank to keep the current
  // credential") doesn't leave the user wondering if an earlier save
  // actually took.
  QLabel *mLiveKitBearerCredentialStatusLabel{nullptr};
  QDialogButtonBox *mButtonBox{nullptr};
  pcm::config::Config mConfig;
  std::shared_ptr<pcm::database::Database> mDb;
  pcm::backup::CredentialStore *mCredentialStore{nullptr};
  TokenBackendCredentialStore *mTokenBackendCredentialStore{nullptr};
  std::unique_ptr<pcm::AppLockService> mAppLockService;
  QString mPendingManualBackupDestinationPath;
  std::optional<pcm::backup::RecoveryEnvelope> mPendingManualBackupEnvelope;
  std::optional<pcm::backup::MasterKey> mPendingEncryptionKey;
  std::optional<pcm::backup::RecoveryEnvelope> mPendingEncryptionEnvelope;
  QString mPendingEncryptionWorkspaceUuid;
  bool mManualBackupKeyReadInProgress{false};
  bool mBackupEncryptionEnableInProgress{false};
};
