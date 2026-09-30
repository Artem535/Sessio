#include "settings_dialog.h"

#include "../widgets/app_settings.h"
#include "backup_validator.h"
#include "restore_service.h"

#include <oclero/qlementine/widgets/ColorEditor.hpp>
#include <oclero/qlementine/widgets/SegmentedControl.hpp>
#include <oclero/qlementine/widgets/Switch.hpp>

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTextEdit>
#include <QThread>
#include <memory>
#include <QTimeEdit>
#include <QUrl>
#include <QVBoxLayout>

#include <Poco/Path.h>

#include <sodium.h>

#include <algorithm>

namespace {
QWidget *makeSettingRow(const QString &title, const QString &description,
                        QWidget *control,
                        QWidget *parent = nullptr) {
  auto *row = new QWidget(parent);
  auto *layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(16);

  auto *textLayout = new QVBoxLayout();
  textLayout->setContentsMargins(0, 0, 0, 0);
  textLayout->setSpacing(2);

  auto *titleLabel = new QLabel(title, row);
  QFont titleFont = titleLabel->font();
  titleFont.setBold(true);
  titleLabel->setFont(titleFont);

  auto *descriptionLabel = new QLabel(description, row);
  descriptionLabel->setWordWrap(true);
  descriptionLabel->setStyleSheet("color: rgba(255, 255, 255, 0.68);");

  textLayout->addWidget(titleLabel);
  textLayout->addWidget(descriptionLabel);

  layout->addLayout(textLayout, 1);
  layout->addWidget(control, 0, Qt::AlignTop);

  return row;
}

void clearSensitiveText(QString *text) {
  text->detach();
  std::fill(text->begin(), text->end(), QChar{});
  text->clear();
}

void clearMasterKey(pcm::backup::MasterKey *key) {
  sodium_memzero(key->bytes.data(), key->bytes.size());
}

void clearSensitiveString(std::string *text) {
  sodium_memzero(text->data(), text->size());
  text->clear();
}

class BackupWorker final : public QObject {
  Q_OBJECT

public:
  BackupWorker(std::shared_ptr<pcm::database::Database> db,
              QString destinationPath, QString attachmentsRoot,
              std::optional<pcm::backup::BackupEncryptionOptions> encryption)
      : mDb(std::move(db)), mDestinationPath(std::move(destinationPath)),
        mAttachmentsRoot(std::move(attachmentsRoot)), mEncryption(std::move(encryption)) {}

  ~BackupWorker() override {
    if (mEncryption.has_value() && mEncryption->master_key.has_value()) {
      clearMasterKey(&*mEncryption->master_key);
    }
  }

public slots:
  void run() {
    pcm::backup::BackupService service;
    pcm::backup::BackupOptions options;
    options.attachments_root = mAttachmentsRoot.toStdString();
    options.encryption = std::move(mEncryption);
    const auto result =
        service.create_backup(*mDb, mDestinationPath.toStdString(), options);
    if (options.encryption.has_value() &&
        options.encryption->master_key.has_value()) {
      clearMasterKey(&*options.encryption->master_key);
    }
    emit finished(result.ok, QString::fromStdString(result.error));
  }

signals:
  void finished(bool ok, const QString &error);

private:
  std::shared_ptr<pcm::database::Database> mDb;
  QString mDestinationPath;
  QString mAttachmentsRoot;
  std::optional<pcm::backup::BackupEncryptionOptions> mEncryption;
};

class ValidateWorker final : public QObject {
  Q_OBJECT

public:
  explicit ValidateWorker(QString backupPath,
                          std::optional<std::string> recoveryPassword = std::nullopt)
      : mBackupPath(std::move(backupPath)),
        mRecoveryPassword(std::move(recoveryPassword)) {}

  ~ValidateWorker() override {
    if (mRecoveryPassword.has_value()) {
      clearSensitiveString(&*mRecoveryPassword);
    }
  }

public slots:
  void run() {
    pcm::backup::BackupValidator validator;
    const auto result = validator.validate(mBackupPath.toStdString(),
                                           mRecoveryPassword);
    if (mRecoveryPassword.has_value()) {
      clearSensitiveString(&*mRecoveryPassword);
      mRecoveryPassword.reset();
    }
    QStringList errors;
    errors.reserve(static_cast<int>(result.errors.size()));
    for (const auto &error : result.errors) {
      errors << QString::fromStdString(error);
    }
    emit finished(result.ok, errors);
  }

signals:
  void finished(bool ok, const QStringList &errors);

private:
  QString mBackupPath;
  std::optional<std::string> mRecoveryPassword;
};
} // namespace

SettingsDialog::SettingsDialog(std::shared_ptr<pcm::database::Database> db,
                               QWidget *parent)
    : SettingsDialog(std::move(db), new QtKeychainTokenBackendCredentialStore(),
                     parent) {}

SettingsDialog::SettingsDialog(std::shared_ptr<pcm::database::Database> db,
                               TokenBackendCredentialStore *tokenBackendCredentialStore,
                               QWidget *parent)
    : QDialog(parent), mDb(std::move(db)),
      mTokenBackendCredentialStore(tokenBackendCredentialStore) {
  mTokenBackendCredentialStore->setParent(this);
  mCredentialStore = new pcm::backup::QtKeychainCredentialStore(this);
  mAppLockService = std::make_unique<pcm::AppLockService>();
  setupUi();
  setupLiveKitSection();
  loadSettings();
  connectSignals();

  connect(mCredentialStore, &pcm::backup::CredentialStore::readFinished, this,
          &SettingsDialog::onManualBackupKeyRead);
  connect(mCredentialStore, &pcm::backup::CredentialStore::writeFinished, this,
          &SettingsDialog::onBackupEncryptionKeyWritten);

  // Bug (fixwave userfeedback group A, bug 1): the bearer-credential field is
  // deliberately write-only (the real secret is never loaded back into the
  // UI), but that used to leave zero feedback about whether a credential was
  // actually stored, so a successful earlier save looked identical to no
  // save at all. An eager read right after construction, plus a re-read
  // after every successful write, keeps mLiveKitBearerCredentialStatusLabel
  // truthful without ever putting the credential value in a widget.
  connect(mTokenBackendCredentialStore, &TokenBackendCredentialStore::readFinished, this,
          &SettingsDialog::onBearerCredentialRead);
  connect(mTokenBackendCredentialStore, &TokenBackendCredentialStore::writeFinished, this,
          [this](const bool ok, const QString & /*error*/) {
            if (ok) {
              mTokenBackendCredentialStore->readBearerCredential();
            }
          });
  mTokenBackendCredentialStore->readBearerCredential();
}

void SettingsDialog::setupUi() {
  setWindowTitle(tr("Settings"));
  setModal(true);
  resize(560, 760);

  auto *rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(20, 20, 20, 20);
  rootLayout->setSpacing(16);

  auto *caption = new QLabel(tr("Application settings"), this);
  QFont captionFont = caption->font();
  captionFont.setPointSize(captionFont.pointSize() + 1);
  captionFont.setBold(true);
  caption->setFont(captionFont);
  rootLayout->addWidget(caption);

  mSettingsSections = new oclero::qlementine::SegmentedControl(this);
  mSettingsSections->addItem(tr("General"), {}, {}, QStringLiteral("general"));
  mSettingsSections->addItem(tr("Privacy & Security"), {}, {}, QStringLiteral("privacy"));
  mSettingsSections->addItem(tr("Backup"), {}, {}, QStringLiteral("backup"));
  mSettingsSections->addItem(tr("Events"), {}, {}, QStringLiteral("events"));
  mSettingsSections->addItem(tr("Online"), {}, {}, QStringLiteral("online"));
  mSettingsSections->addItem(tr("LiveKit"), {}, {}, QStringLiteral("livekit"));
  mSettingsSections->setItemsShouldExpand(true);
  rootLayout->addWidget(mSettingsSections);

  mSettingsStack = new QStackedWidget(this);
  rootLayout->addWidget(mSettingsStack, 1);

  auto *generalPage = new QWidget(mSettingsStack);
  auto *generalSettingsLayout = new QVBoxLayout(generalPage);
  generalSettingsLayout->setContentsMargins(0, 0, 0, 0);
  generalSettingsLayout->setSpacing(16);

  auto *privacyPage = new QWidget(mSettingsStack);
  auto *privacySettingsLayout = new QVBoxLayout(privacyPage);
  privacySettingsLayout->setContentsMargins(0, 0, 0, 0);
  privacySettingsLayout->setSpacing(16);

  auto *backupPage = new QWidget(mSettingsStack);
  auto *backupSettingsLayout = new QVBoxLayout(backupPage);
  backupSettingsLayout->setContentsMargins(0, 0, 0, 0);
  backupSettingsLayout->setSpacing(16);

  auto *eventsPage = new QWidget(mSettingsStack);
  auto *eventSettingsLayout = new QVBoxLayout(eventsPage);
  eventSettingsLayout->setContentsMargins(0, 0, 0, 0);
  eventSettingsLayout->setSpacing(16);

  auto *onlinePage = new QWidget(mSettingsStack);
  auto *onlineSettingsLayout = new QVBoxLayout(onlinePage);
  onlineSettingsLayout->setContentsMargins(0, 0, 0, 0);
  onlineSettingsLayout->setSpacing(16);

  mSettingsStack->addWidget(generalPage);
  mSettingsStack->addWidget(privacyPage);
  mSettingsStack->addWidget(backupPage);
  mSettingsStack->addWidget(eventsPage);
  mSettingsStack->addWidget(onlinePage);

  auto *languageBox = new QGroupBox(tr("Language"), generalPage);
  auto *languageLayout = new QVBoxLayout(languageBox);
  languageLayout->setContentsMargins(16, 16, 16, 16);
  languageLayout->setSpacing(14);
  mLanguageCombo = new QComboBox(languageBox);
  mLanguageCombo->addItem(tr("System default"), QStringLiteral("system"));
  mLanguageCombo->addItem(tr("Russian"), QStringLiteral("ru"));
  mLanguageCombo->addItem(tr("English"), QStringLiteral("en"));
  languageLayout->addWidget(
      makeSettingRow(tr("Interface language"),
                     tr("The selected language will be applied after restarting the application."),
                     mLanguageCombo, languageBox));
  generalSettingsLayout->addWidget(languageBox);

  auto *databaseBox = new QGroupBox(tr("Database"), generalPage);
  auto *databaseLayout = new QVBoxLayout(databaseBox);
  databaseLayout->setContentsMargins(16, 16, 16, 16);
  databaseLayout->setSpacing(10);
  auto *dbPathTitle = new QLabel(tr("Database directory"), databaseBox);
  QFont dbPathTitleFont = dbPathTitle->font();
  dbPathTitleFont.setBold(true);
  dbPathTitle->setFont(dbPathTitleFont);
  mDatabasePathLabel = new QLabel(databaseBox);
  mDatabasePathLabel->setWordWrap(true);
  mDatabasePathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  mDatabasePathLabel->setStyleSheet("color: rgba(255, 255, 255, 0.68);");
  mOpenDatabaseFolderButton = new QPushButton(tr("Open folder"), databaseBox);
  databaseLayout->addWidget(dbPathTitle);
  databaseLayout->addWidget(mDatabasePathLabel);
  databaseLayout->addWidget(mOpenDatabaseFolderButton, 0, Qt::AlignLeft);
  generalSettingsLayout->addWidget(databaseBox);

  auto *backupBox = new QGroupBox(tr("Backup"), backupPage);
  auto *backupLayout = new QVBoxLayout(backupBox);
  backupLayout->setContentsMargins(16, 16, 16, 16);
  backupLayout->setSpacing(10);
  auto *backupDescription = new QLabel(
      tr("Create a full backup (database and attachments) as a single "
        ".psybackup file, or validate an existing one."),
      backupBox);
  backupDescription->setWordWrap(true);
  backupDescription->setStyleSheet("color: rgba(255, 255, 255, 0.68);");
  auto *backupButtonsRow = new QWidget(backupBox);
  auto *backupButtonsLayout = new QHBoxLayout(backupButtonsRow);
  backupButtonsLayout->setContentsMargins(0, 0, 0, 0);
  backupButtonsLayout->setSpacing(10);
  mCreateBackupButton = new QPushButton(tr("Create backup..."), backupBox);
  mCreateBackupButton->setObjectName(QStringLiteral("createBackupButton"));
  mValidateBackupButton = new QPushButton(tr("Validate backup..."), backupBox);
  mRestoreBackupButton = new QPushButton(tr("Restore backup..."), backupBox);
  backupButtonsLayout->addWidget(mCreateBackupButton);
  backupButtonsLayout->addWidget(mValidateBackupButton);
  backupButtonsLayout->addWidget(mRestoreBackupButton);
  backupButtonsLayout->addStretch();
  mBackupStatusLabel = new QLabel(backupBox);
  mBackupStatusLabel->setStyleSheet("color: rgba(255, 255, 255, 0.68);");
  mBackupStatusLabel->setVisible(false);
  mBackupProgressBar = new QProgressBar(backupBox);
  mBackupProgressBar->setRange(0, 0);
  mBackupProgressBar->setTextVisible(false);
  mBackupProgressBar->setVisible(false);
  backupLayout->addWidget(backupDescription);
  backupLayout->addWidget(backupButtonsRow);
  backupLayout->addWidget(mBackupStatusLabel);
  backupLayout->addWidget(mBackupProgressBar);

  mBackupEncryptionEnabledSwitch = new oclero::qlementine::Switch(backupBox);
  mBackupEncryptionEnabledSwitch->setObjectName(QStringLiteral("backupEncryptionEnabledSwitch"));
  mBackupEncryptionDetails = new QWidget(backupBox);
  auto *encryptionDetailsLayout = new QVBoxLayout(mBackupEncryptionDetails);
  encryptionDetailsLayout->setContentsMargins(0, 0, 0, 0);
  encryptionDetailsLayout->setSpacing(10);
  mBackupEncryptionWarningLabel = new QLabel(
      tr("The recovery password cannot be recovered. Store it safely."),
      mBackupEncryptionDetails);
  mBackupEncryptionWarningLabel->setWordWrap(true);
  mBackupEncryptionWarningLabel->setStyleSheet("color: #f0c36d;");
  mBackupEncryptionPasswordEdit = new QLineEdit(mBackupEncryptionDetails);
  mBackupEncryptionPasswordEdit->setEchoMode(QLineEdit::Password);
  mBackupEncryptionConfirmationEdit = new QLineEdit(mBackupEncryptionDetails);
  mBackupEncryptionConfirmationEdit->setEchoMode(QLineEdit::Password);
  mEnableBackupEncryptionButton =
      new QPushButton(tr("Enable encryption"), mBackupEncryptionDetails);
  encryptionDetailsLayout->addWidget(mBackupEncryptionWarningLabel);
  encryptionDetailsLayout->addWidget(makeSettingRow(
      tr("Recovery password"), tr("Use at least 12 characters."),
      mBackupEncryptionPasswordEdit, mBackupEncryptionDetails));
  encryptionDetailsLayout->addWidget(makeSettingRow(
      tr("Confirm recovery password"), tr("Enter the same password again."),
      mBackupEncryptionConfirmationEdit, mBackupEncryptionDetails));
  encryptionDetailsLayout->addWidget(mEnableBackupEncryptionButton, 0,
                                     Qt::AlignRight);
  backupLayout->addWidget(makeSettingRow(
      tr("Encrypt backups"),
      tr("Protect new backups with a recovery password and the system keychain."),
      mBackupEncryptionEnabledSwitch, backupBox));
  backupLayout->addWidget(mBackupEncryptionDetails);
  backupSettingsLayout->addWidget(backupBox);

  auto *autoBackupBox = new QGroupBox(tr("Automatic Backups"), backupPage);
  auto *autoBackupLayout = new QVBoxLayout(autoBackupBox);
  autoBackupLayout->setContentsMargins(16, 16, 16, 16);
  autoBackupLayout->setSpacing(14);
  mAutoBackupEnabledSwitch = new oclero::qlementine::Switch(autoBackupBox);
  mAutoBackupEnabledSwitch->setObjectName(QStringLiteral("autoBackupEnabledSwitch"));
  mAutoBackupIntervalSpinBox = new QSpinBox(autoBackupBox);
  mAutoBackupIntervalSpinBox->setMinimum(1);
  mAutoBackupIntervalSpinBox->setMaximum(90);
  mAutoBackupIntervalSpinBox->setSuffix(tr(" days"));
  mAutoBackupKeepCountSpinBox = new QSpinBox(autoBackupBox);
  mAutoBackupKeepCountSpinBox->setMinimum(1);
  mAutoBackupKeepCountSpinBox->setMaximum(50);
  auto *destinationRow = new QWidget(autoBackupBox);
  auto *destinationLayout = new QHBoxLayout(destinationRow);
  destinationLayout->setContentsMargins(0, 0, 0, 0);
  destinationLayout->setSpacing(10);
  mAutoBackupDestinationEdit = new QLineEdit(destinationRow);
  mAutoBackupDestinationEdit->setReadOnly(true);
  mAutoBackupBrowseButton = new QPushButton(tr("Browse..."), destinationRow);
  destinationLayout->addWidget(mAutoBackupDestinationEdit, 1);
  destinationLayout->addWidget(mAutoBackupBrowseButton);
  autoBackupLayout->addWidget(makeSettingRow(
      tr("Automatic backups"),
      tr("Periodically create a backup in the background without needing "
        "to click \"Create backup...\"."),
      mAutoBackupEnabledSwitch, autoBackupBox));
  autoBackupLayout->addWidget(
      makeSettingRow(tr("Backup interval"), tr("How often an automatic backup is taken."),
                    mAutoBackupIntervalSpinBox, autoBackupBox));
  autoBackupLayout->addWidget(makeSettingRow(
      tr("Keep last"),
      tr("How many automatic backups to keep before older ones are deleted."),
      mAutoBackupKeepCountSpinBox, autoBackupBox));
  autoBackupLayout->addWidget(makeSettingRow(tr("Destination folder"),
                                             tr("Where automatic backups are saved."),
                                             destinationRow, autoBackupBox));
  backupSettingsLayout->addWidget(autoBackupBox);
  backupSettingsLayout->addStretch();

  auto *notificationsBox = new QGroupBox(tr("Notifications"), privacyPage);
  auto *notificationsLayout = new QVBoxLayout(notificationsBox);
  notificationsLayout->setContentsMargins(16, 16, 16, 16);
  notificationsLayout->setSpacing(14);
  mNotificationsEnabledSwitch = new oclero::qlementine::Switch(notificationsBox);
  mNotificationsEnabledSwitch->setObjectName(QStringLiteral("notificationsEnabledSwitch"));
  mNotificationLeadMinutesSpinBox = new QSpinBox(notificationsBox);
  mNotificationLeadMinutesSpinBox->setMinimum(1);
  mNotificationLeadMinutesSpinBox->setMaximum(24 * 60);
  mNotificationLeadMinutesSpinBox->setSingleStep(5);
  mNotificationLeadMinutesSpinBox->setSuffix(tr(" min"));
  mNotificationPrivacyModeCombo = new QComboBox(notificationsBox);
  mNotificationPrivacyModeCombo->addItem(tr("Full details"),
                                         static_cast<int>(pcm::NotificationPrivacyMode::Full));
  mNotificationPrivacyModeCombo->addItem(tr("Hidden (recommended)"),
                                         static_cast<int>(pcm::NotificationPrivacyMode::Hidden));
  mNotificationPrivacyModeCombo->addItem(tr("Minimal (time only)"),
                                         static_cast<int>(pcm::NotificationPrivacyMode::Minimal));
  notificationsLayout->addWidget(
      makeSettingRow(tr("Session reminders"),
                     tr("Show a desktop notification before a scheduled session starts."),
                     mNotificationsEnabledSwitch, notificationsBox));
  notificationsLayout->addWidget(
      makeSettingRow(tr("Notify before start"),
                     tr("How many minutes before the session the reminder should appear."),
                     mNotificationLeadMinutesSpinBox, notificationsBox));
  notificationsLayout->addWidget(makeSettingRow(
      tr("Notification content"),
      tr("How much a reminder reveals on a shared or locked screen. Client name and "
        "session title are never shown outside Full details."),
      mNotificationPrivacyModeCombo, notificationsBox));
  privacySettingsLayout->addWidget(notificationsBox);

  auto *appLockBox = new QGroupBox(tr("App lock"), privacyPage);
  auto *privacyLayout = new QVBoxLayout(appLockBox);
  privacyLayout->setContentsMargins(16, 16, 16, 16);
  privacyLayout->setSpacing(14);
  mAppLockEnabledSwitch = new oclero::qlementine::Switch(appLockBox);
  mAppLockEnabledSwitch->setObjectName(QStringLiteral("appLockEnabledSwitch"));
  mAppLockTimeoutSpinBox = new QSpinBox(appLockBox);
  mAppLockTimeoutSpinBox->setRange(1, 24 * 60);
  mAppLockTimeoutSpinBox->setSuffix(tr(" min"));
  mChangeAppLockCredentialButton = new QPushButton(tr("Change PIN or password"), appLockBox);
  mClearSensitiveClipboardSwitch = new oclero::qlementine::Switch(appLockBox);
  mSensitiveClipboardDelaySpinBox = new QSpinBox(appLockBox);
  mSensitiveClipboardDelaySpinBox->setRange(5, 10 * 60);
  mSensitiveClipboardDelaySpinBox->setSuffix(tr(" sec"));
  privacyLayout->addWidget(makeSettingRow(
      tr("Lock application"),
      tr("Require a PIN or password after inactivity or from the system tray."),
      mAppLockEnabledSwitch, appLockBox));
  privacyLayout->addWidget(makeSettingRow(
      tr("Lock after"), tr("Time without keyboard or mouse activity."),
      mAppLockTimeoutSpinBox, appLockBox));
  privacyLayout->addWidget(mChangeAppLockCredentialButton, 0, Qt::AlignRight);
  privacyLayout->addWidget(makeSettingRow(
      tr("Clear copied meeting details"),
      tr("Clear meeting links and invitations copied by the application."),
      mClearSensitiveClipboardSwitch, appLockBox));
  privacyLayout->addWidget(makeSettingRow(
      tr("Clear after"), tr("Delay before copied meeting details are removed."),
      mSensitiveClipboardDelaySpinBox, appLockBox));
  privacySettingsLayout->addWidget(appLockBox);
  generalSettingsLayout->addStretch();
  privacySettingsLayout->addStretch();

  auto *eventsBox = new QGroupBox(tr("Timeline colors"), eventsPage);
  auto *eventsLayout = new QVBoxLayout(eventsBox);
  eventsLayout->setContentsMargins(16, 16, 16, 16);
  eventsLayout->setSpacing(14);
  mPreventOverlapsSwitch = new oclero::qlementine::Switch(eventsBox);
  mWorkEventColorEditor = new oclero::qlementine::ColorEditor(eventsBox);
  mPersonalEventColorEditor = new oclero::qlementine::ColorEditor(eventsBox);
  mCurrencyCombo = new QComboBox(eventsBox);
  mCurrencyCombo->addItem(tr("Russian Ruble (₽)"), QStringLiteral("RUB"));
  mCurrencyCombo->addItem(tr("US Dollar ($)"), QStringLiteral("USD"));
  mCurrencyCombo->addItem(tr("Euro (€)"), QStringLiteral("EUR"));
  mCurrencyCombo->addItem(tr("British Pound (£)"), QStringLiteral("GBP"));
  mDefaultWorkCostSpinBox = new QDoubleSpinBox(eventsBox);
  mDefaultWorkCostSpinBox->setDecimals(2);
  mDefaultWorkCostSpinBox->setMinimum(0.0);
  mDefaultWorkCostSpinBox->setMaximum(1'000'000.0);
  mDefaultWorkCostSpinBox->setSingleStep(100.0);
  mDefaultWorkCostSpinBox->setSuffix(QStringLiteral(" ") + pcm::app_settings::currencySymbol());
  mWorkDayStartEdit = new QTimeEdit(eventsBox);
  mWorkDayStartEdit->setDisplayFormat("HH:mm");
  mWorkDayEndEdit = new QTimeEdit(eventsBox);
  mWorkDayEndEdit->setDisplayFormat("HH:mm");
  mDefaultSessionDurationSpinBox = new QSpinBox(eventsBox);
  mDefaultSessionDurationSpinBox->setMinimum(5);
  mDefaultSessionDurationSpinBox->setMaximum(480);
  mDefaultSessionDurationSpinBox->setSingleStep(5);
  mDefaultSessionDurationSpinBox->setSuffix(tr(" min"));
  mDefaultBufferBeforeSpinBox = new QSpinBox(eventsBox);
  mDefaultBufferBeforeSpinBox->setRange(0, 240);
  mDefaultBufferBeforeSpinBox->setSuffix(tr(" min"));
  mDefaultBufferAfterSpinBox = new QSpinBox(eventsBox);
  mDefaultBufferAfterSpinBox->setRange(0, 240);
  mDefaultBufferAfterSpinBox->setSuffix(tr(" min"));
  eventsLayout->addWidget(
      makeSettingRow(tr("Disallow overlapping events"),
                     tr("Reject saves when the selected time range intersects another event."),
                     mPreventOverlapsSwitch, eventsBox));
  eventsLayout->addWidget(
      makeSettingRow(tr("Work day start"),
                     tr("Start time used for quick session suggestions."),
                     mWorkDayStartEdit, eventsBox));
  eventsLayout->addWidget(
      makeSettingRow(tr("Work day end"),
                     tr("End time used for quick session suggestions."),
                     mWorkDayEndEdit, eventsBox));
  eventsLayout->addWidget(
      makeSettingRow(tr("Default session duration"),
                     tr("Duration used for quick session suggestions and new sessions."),
                     mDefaultSessionDurationSpinBox, eventsBox));
  eventsLayout->addWidget(
      makeSettingRow(tr("Default buffer before"),
                     tr("Time reserved before each new session and Quick Slot."),
                     mDefaultBufferBeforeSpinBox, eventsBox));
  eventsLayout->addWidget(
      makeSettingRow(tr("Default buffer after"),
                     tr("Time reserved after each new session and Quick Slot."),
                     mDefaultBufferAfterSpinBox, eventsBox));
  eventsLayout->addWidget(
      makeSettingRow(tr("Currency"),
                     tr("Symbol shown next to cost values throughout the app."),
                     mCurrencyCombo, eventsBox));
  eventsLayout->addWidget(
      makeSettingRow(tr("Default work event cost"),
                     tr("Used to prefill new work sessions."),
                     mDefaultWorkCostSpinBox, eventsBox));
  eventsLayout->addWidget(
      makeSettingRow(tr("Work events"),
                     tr("Accent color for work sessions in the timeline."),
                     mWorkEventColorEditor, eventsBox));
  eventsLayout->addWidget(
      makeSettingRow(tr("Personal events"),
                     tr("Accent color for personal events in the timeline."),
                     mPersonalEventColorEditor, eventsBox));
  eventSettingsLayout->addWidget(eventsBox);
  eventSettingsLayout->addStretch();

  auto *onlineBox = new QGroupBox(tr("Online sessions"), onlinePage);
  auto *onlineLayout = new QVBoxLayout(onlineBox);
  onlineLayout->setContentsMargins(16, 16, 16, 16);
  onlineLayout->setSpacing(10);
  auto *templateTitle = new QLabel(tr("Invite template"), onlineBox);
  QFont templateTitleFont = templateTitle->font();
  templateTitleFont.setBold(true);
  templateTitle->setFont(templateTitleFont);
  auto *templateDescription = new QLabel(
      tr("Available variables: {client_name}, {date}, {time}, {meeting_url}"),
      onlineBox);
  templateDescription->setWordWrap(true);
  templateDescription->setStyleSheet("color: rgba(255, 255, 255, 0.68);");
  mMeetingInviteTemplateEdit = new QTextEdit(onlineBox);
  mMeetingInviteTemplateEdit->setAcceptRichText(false);
  mMeetingInviteTemplateEdit->setMinimumHeight(120);
  onlineLayout->addWidget(templateTitle);
  onlineLayout->addWidget(templateDescription);
  onlineLayout->addWidget(mMeetingInviteTemplateEdit);
  onlineSettingsLayout->addWidget(onlineBox);
  onlineSettingsLayout->addStretch();

  mButtonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
  rootLayout->addWidget(mButtonBox);
}

void SettingsDialog::setupLiveKitSection() {
  auto *liveKitPage = new QWidget(mSettingsStack);
  auto *liveKitPageLayout = new QVBoxLayout(liveKitPage);
  liveKitPageLayout->setContentsMargins(0, 0, 0, 0);
  liveKitPageLayout->setSpacing(16);

  auto *liveKitBox = new QGroupBox(tr("Token backend"), liveKitPage);
  auto *liveKitLayout = new QVBoxLayout(liveKitBox);
  liveKitLayout->setContentsMargins(16, 16, 16, 16);
  liveKitLayout->setSpacing(10);

  auto *baseUrlTitle = new QLabel(tr("Token backend URL"), liveKitBox);
  QFont baseUrlTitleFont = baseUrlTitle->font();
  baseUrlTitleFont.setBold(true);
  baseUrlTitle->setFont(baseUrlTitleFont);
  auto *baseUrlDescription = new QLabel(
      tr("Base URL of the LiveKit token-issuing backend."), liveKitBox);
  baseUrlDescription->setWordWrap(true);
  baseUrlDescription->setStyleSheet("color: rgba(255, 255, 255, 0.68);");
  mLiveKitBaseUrlEdit = new QLineEdit(liveKitBox);
  mLiveKitBaseUrlEdit->setObjectName(QStringLiteral("liveKitBaseUrlEdit"));
  mLiveKitBaseUrlEdit->setPlaceholderText(tr("https://token-backend.example.com"));

  auto *credentialTitle = new QLabel(tr("Bearer credential"), liveKitBox);
  QFont credentialTitleFont = credentialTitle->font();
  credentialTitleFont.setBold(true);
  credentialTitle->setFont(credentialTitleFont);
  auto *credentialDescription = new QLabel(
      tr("Stored in the system keychain. Leave blank to keep the current credential."),
      liveKitBox);
  credentialDescription->setWordWrap(true);
  credentialDescription->setStyleSheet("color: rgba(255, 255, 255, 0.68);");
  mLiveKitBearerCredentialEdit = new QLineEdit(liveKitBox);
  mLiveKitBearerCredentialEdit->setObjectName(QStringLiteral("liveKitBearerCredentialEdit"));
  mLiveKitBearerCredentialEdit->setEchoMode(QLineEdit::Password);

  // Reports whether a credential is stored, never the credential itself --
  // see mLiveKitBearerCredentialStatusLabel's doc comment in the header. Set
  // for the first time once the initial readBearerCredential() call
  // (triggered at the end of the constructor) completes.
  mLiveKitBearerCredentialStatusLabel = new QLabel(liveKitBox);
  mLiveKitBearerCredentialStatusLabel->setObjectName(
      QStringLiteral("liveKitBearerCredentialStatusLabel"));
  mLiveKitBearerCredentialStatusLabel->setStyleSheet("color: rgba(255, 255, 255, 0.68);");

  auto *liveKitSaveButton = new QPushButton(tr("Save"), liveKitBox);
  liveKitSaveButton->setObjectName(QStringLiteral("liveKitSaveButton"));
  connect(liveKitSaveButton, &QPushButton::clicked, this,
          &SettingsDialog::saveLiveKitSettings);

  liveKitLayout->addWidget(baseUrlTitle);
  liveKitLayout->addWidget(baseUrlDescription);
  liveKitLayout->addWidget(mLiveKitBaseUrlEdit);
  liveKitLayout->addWidget(credentialTitle);
  liveKitLayout->addWidget(credentialDescription);
  liveKitLayout->addWidget(mLiveKitBearerCredentialEdit);
  liveKitLayout->addWidget(mLiveKitBearerCredentialStatusLabel);
  liveKitLayout->addWidget(liveKitSaveButton, 0, Qt::AlignRight);

  liveKitPageLayout->addWidget(liveKitBox);
  liveKitPageLayout->addStretch();

  mSettingsStack->addWidget(liveKitPage);
}

void SettingsDialog::loadSettings() const {
  const auto languageCode = pcm::app_settings::languageCode();
  const auto languageIndex = mLanguageCombo->findData(languageCode);
  mLanguageCombo->setCurrentIndex(languageIndex >= 0 ? languageIndex : 0);
  mDatabasePathLabel->setText(
      QString::fromStdString(mConfig.db_conf.value_.db_pth.toString()));
  mNotificationsEnabledSwitch->setChecked(pcm::app_settings::notificationsEnabled());
  mNotificationLeadMinutesSpinBox->setValue(
      pcm::app_settings::notificationLeadMinutes());
  mNotificationLeadMinutesSpinBox->setEnabled(
      mNotificationsEnabledSwitch->isChecked());
  const auto privacyModeIndex = mNotificationPrivacyModeCombo->findData(
      static_cast<int>(pcm::app_settings::notificationPrivacyMode()));
  mNotificationPrivacyModeCombo->setCurrentIndex(privacyModeIndex >= 0 ? privacyModeIndex : 0);
  mNotificationPrivacyModeCombo->setEnabled(mNotificationsEnabledSwitch->isChecked());
  const bool appLockEnabled = mAppLockService->isConfigured();
  mAppLockEnabledSwitch->setChecked(appLockEnabled);
  mAppLockTimeoutSpinBox->setValue(pcm::app_settings::appLockTimeoutMinutes());
  mAppLockTimeoutSpinBox->setEnabled(appLockEnabled);
  mChangeAppLockCredentialButton->setEnabled(appLockEnabled);
  mClearSensitiveClipboardSwitch->setChecked(
      pcm::app_settings::clearSensitiveClipboard());
  mSensitiveClipboardDelaySpinBox->setValue(
      pcm::app_settings::sensitiveClipboardClearDelaySeconds());
  mSensitiveClipboardDelaySpinBox->setEnabled(
      mClearSensitiveClipboardSwitch->isChecked());
  mAutoBackupEnabledSwitch->setChecked(pcm::app_settings::autoBackupEnabled());
  mAutoBackupIntervalSpinBox->setValue(pcm::app_settings::autoBackupIntervalDays());
  mAutoBackupKeepCountSpinBox->setValue(pcm::app_settings::autoBackupKeepCount());
  mAutoBackupDestinationEdit->setText(pcm::app_settings::autoBackupDestination());
  const auto autoBackupEnabled = mAutoBackupEnabledSwitch->isChecked();
  mAutoBackupIntervalSpinBox->setEnabled(autoBackupEnabled);
  mAutoBackupKeepCountSpinBox->setEnabled(autoBackupEnabled);
  mAutoBackupDestinationEdit->setEnabled(autoBackupEnabled);
  mAutoBackupBrowseButton->setEnabled(autoBackupEnabled);
  mBackupEncryptionEnabledSwitch->setChecked(
      pcm::app_settings::backupEncryptionEnabled());
  updateBackupEncryptionUi();
  mPreventOverlapsSwitch->setChecked(pcm::app_settings::preventEventOverlaps());
  mWorkDayStartEdit->setTime(pcm::app_settings::workDayStart());
  mWorkDayEndEdit->setTime(pcm::app_settings::workDayEnd());
  mDefaultSessionDurationSpinBox->setValue(
      pcm::app_settings::defaultSessionDurationMinutes());
  mDefaultBufferBeforeSpinBox->setValue(
      pcm::app_settings::defaultBufferBeforeMinutes());
  mDefaultBufferAfterSpinBox->setValue(
      pcm::app_settings::defaultBufferAfterMinutes());
  const auto currencyIndex = mCurrencyCombo->findData(pcm::app_settings::currencyCode());
  mCurrencyCombo->setCurrentIndex(currencyIndex >= 0 ? currencyIndex : 0);
  mDefaultWorkCostSpinBox->setValue(pcm::app_settings::defaultWorkEventCost());
  mWorkEventColorEditor->setColor(pcm::app_settings::workEventColor());
  mPersonalEventColorEditor->setColor(pcm::app_settings::personalEventColor());
  mMeetingInviteTemplateEdit->setPlainText(
      pcm::app_settings::meetingInviteTemplate());
  // A corrupt Config.yaml must not take the whole dialog down: leave the
  // field at Config's default (empty) and let the rest of Settings work.
  try {
    mLiveKitBaseUrlEdit->setText(QString::fromStdString(
        pcm::config::Config::read_config().token_backend_base_url));
  } catch (const std::exception &error) {
    qWarning() << "SettingsDialog: failed to read config, token backend URL left empty:"
               << error.what();
    mLiveKitBaseUrlEdit->clear();
  }
}

void SettingsDialog::connectSignals() {
  connect(mButtonBox, &QDialogButtonBox::rejected, this, &QDialog::accept);
  connect(mSettingsSections, &oclero::qlementine::SegmentedControl::currentIndexChanged,
          this, [this]() {
            mSettingsStack->setCurrentIndex(mSettingsSections->currentIndex());
          });
  connect(mLanguageCombo, &QComboBox::currentIndexChanged, this, [this](const int index) {
    pcm::app_settings::setLanguageCode(mLanguageCombo->itemData(index).toString());
  });
  connect(mOpenDatabaseFolderButton, &QPushButton::clicked, this,
          &SettingsDialog::openDatabaseFolder);
  connect(mCreateBackupButton, &QPushButton::clicked, this,
          &SettingsDialog::createBackup);
  connect(mValidateBackupButton, &QPushButton::clicked, this,
          &SettingsDialog::validateBackup);
  connect(mRestoreBackupButton, &QPushButton::clicked, this,
          &SettingsDialog::restoreBackup);
  connect(mBackupEncryptionEnabledSwitch, &QAbstractButton::toggled, this,
          [this](const bool checked) {
            if (!checked) {
              pcm::app_settings::setBackupEncryptionEnabled(false);
              mBackupEncryptionPasswordEdit->clear();
              mBackupEncryptionConfirmationEdit->clear();
            }
            updateBackupEncryptionUi();
          });
  connect(mEnableBackupEncryptionButton, &QPushButton::clicked, this,
          &SettingsDialog::enableBackupEncryption);
  connect(mNotificationsEnabledSwitch, &QAbstractButton::toggled, this,
          [this](const bool checked) {
            pcm::app_settings::setNotificationsEnabled(checked);
            mNotificationLeadMinutesSpinBox->setEnabled(checked);
            mNotificationPrivacyModeCombo->setEnabled(checked);
          });
  connect(mNotificationLeadMinutesSpinBox, &QSpinBox::valueChanged, this,
          [](const int minutes) {
            pcm::app_settings::setNotificationLeadMinutes(minutes);
          });
  connect(mNotificationPrivacyModeCombo, &QComboBox::currentIndexChanged, this,
          [this](const int index) {
            const auto mode = static_cast<pcm::NotificationPrivacyMode>(
                mNotificationPrivacyModeCombo->itemData(index).toInt());
            pcm::app_settings::setNotificationPrivacyMode(mode);
          });
  connect(mAppLockEnabledSwitch, &QAbstractButton::toggled, this,
          [this](const bool enabled) {
            if (enabled && !mAppLockService->isConfigured()) {
              configureAppLock();
              const QSignalBlocker blocker(mAppLockEnabledSwitch);
              mAppLockEnabledSwitch->setChecked(mAppLockService->isConfigured());
            } else if (!enabled && mAppLockService->isConfigured()) {
              mAppLockService->disable();
            }
            const bool configured = mAppLockService->isConfigured();
            mAppLockTimeoutSpinBox->setEnabled(configured);
            mChangeAppLockCredentialButton->setEnabled(configured);
          });
  connect(mAppLockTimeoutSpinBox, &QSpinBox::valueChanged, this,
          [](const int minutes) {
            pcm::app_settings::setAppLockTimeoutMinutes(minutes);
          });
  connect(mChangeAppLockCredentialButton, &QPushButton::clicked, this,
          [this]() { configureAppLock(); });
  connect(mClearSensitiveClipboardSwitch, &QAbstractButton::toggled, this,
          [this](const bool enabled) {
            pcm::app_settings::setClearSensitiveClipboard(enabled);
            mSensitiveClipboardDelaySpinBox->setEnabled(enabled);
          });
  connect(mSensitiveClipboardDelaySpinBox, &QSpinBox::valueChanged, this,
          [](const int seconds) {
            pcm::app_settings::setSensitiveClipboardClearDelaySeconds(seconds);
          });
  connect(mAutoBackupEnabledSwitch, &QAbstractButton::toggled, this,
          [this](const bool checked) {
            pcm::app_settings::setAutoBackupEnabled(checked);
            mAutoBackupIntervalSpinBox->setEnabled(checked);
            mAutoBackupKeepCountSpinBox->setEnabled(checked);
            mAutoBackupDestinationEdit->setEnabled(checked);
            mAutoBackupBrowseButton->setEnabled(checked);
          });
  connect(mAutoBackupIntervalSpinBox, &QSpinBox::valueChanged, this,
          [](const int days) {
            pcm::app_settings::setAutoBackupIntervalDays(days);
          });
  connect(mAutoBackupKeepCountSpinBox, &QSpinBox::valueChanged, this,
          [](const int count) {
            pcm::app_settings::setAutoBackupKeepCount(count);
          });
  connect(mAutoBackupBrowseButton, &QPushButton::clicked, this,
          &SettingsDialog::browseAutoBackupDestination);
  connect(mPreventOverlapsSwitch, &QAbstractButton::toggled, this,
          [](const bool checked) {
            pcm::app_settings::setPreventEventOverlaps(checked);
          });
  connect(mWorkDayStartEdit, &QTimeEdit::timeChanged, this,
          [](const QTime &time) {
            pcm::app_settings::setWorkDayStart(time);
          });
  connect(mWorkDayEndEdit, &QTimeEdit::timeChanged, this,
          [](const QTime &time) {
            pcm::app_settings::setWorkDayEnd(time);
          });
  connect(mDefaultSessionDurationSpinBox, &QSpinBox::valueChanged, this,
          [](const int minutes) {
            pcm::app_settings::setDefaultSessionDurationMinutes(minutes);
          });
  connect(mDefaultBufferBeforeSpinBox, &QSpinBox::valueChanged, this,
          [](const int minutes) {
            pcm::app_settings::setDefaultBufferBeforeMinutes(minutes);
          });
  connect(mDefaultBufferAfterSpinBox, &QSpinBox::valueChanged, this,
          [](const int minutes) {
            pcm::app_settings::setDefaultBufferAfterMinutes(minutes);
          });
  connect(mCurrencyCombo, &QComboBox::currentIndexChanged, this, [this](const int index) {
    pcm::app_settings::setCurrencyCode(mCurrencyCombo->itemData(index).toString());
    mDefaultWorkCostSpinBox->setSuffix(QStringLiteral(" ") + pcm::app_settings::currencySymbol());
  });
  connect(mDefaultWorkCostSpinBox, &QDoubleSpinBox::valueChanged, this,
          [](const double value) {
            pcm::app_settings::setDefaultWorkEventCost(value);
          });
  connect(mWorkEventColorEditor, &oclero::qlementine::ColorEditor::colorChanged,
          this, [this]() {
            pcm::app_settings::setWorkEventColor(mWorkEventColorEditor->color());
          });
  connect(mPersonalEventColorEditor, &oclero::qlementine::ColorEditor::colorChanged,
          this, [this]() {
            pcm::app_settings::setPersonalEventColor(mPersonalEventColorEditor->color());
          });
  connect(mMeetingInviteTemplateEdit, &QTextEdit::textChanged, this, [this]() {
    pcm::app_settings::setMeetingInviteTemplate(
        mMeetingInviteTemplateEdit->toPlainText());
  });
}

void SettingsDialog::configureAppLock() {
  bool accepted = false;
  QString credential = QInputDialog::getText(
      this, tr("Set application lock"),
      tr("Enter a PIN of at least 6 digits or a password of at least 12 characters."),
      QLineEdit::Password, {}, &accepted);
  if (!accepted) {
    return;
  }
  auto confirmation = QInputDialog::getText(
      this, tr("Confirm application lock"), tr("Enter the same PIN or password again."),
      QLineEdit::Password, {}, &accepted);
  if (!accepted || credential != confirmation) {
    QMessageBox::warning(this, tr("Application lock"),
                         tr("The PIN or password entries do not match."));
    credential.fill(QChar{});
    confirmation.fill(QChar{});
    return;
  }
  auto credentialBytes = credential.toUtf8();
  const bool configured = mAppLockService->configure(
      std::string_view{credentialBytes.constData(),
                       static_cast<std::size_t>(credentialBytes.size())});
  credentialBytes.fill('\0');
  credential.fill(QChar{});
  confirmation.fill(QChar{});
  if (!configured) {
    QMessageBox::warning(this, tr("Application lock"),
                         tr("Use a PIN of at least 6 digits or a password of at least 12 characters."));
  }
}

void SettingsDialog::openDatabaseFolder() const {
  const auto path = QString::fromStdString(mConfig.db_conf.value_.db_pth.toString());
  QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void SettingsDialog::createBackup() {
  const auto defaultName =
      QStringLiteral("Sessio-%1.psybackup")
          .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss"));
  const auto defaultDir =
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  const auto destinationPath = QFileDialog::getSaveFileName(
      this, tr("Create Backup"), QDir(defaultDir).filePath(defaultName),
      tr("Sessio Backup (*.psybackup)"));
  if (destinationPath.isEmpty()) {
    return;
  }

  mCreateBackupButton->setEnabled(false);
  mValidateBackupButton->setEnabled(false);
  mBackupStatusLabel->setText(tr("Creating backup…"));
  mBackupStatusLabel->setVisible(true);
  mBackupProgressBar->setVisible(true);

  if (pcm::app_settings::backupEncryptionEnabled()) {
    std::optional<pcm::backup::RecoveryEnvelope> recoveryEnvelope;
    QString workspaceUuid;
    try {
      recoveryEnvelope = pcm::backup::deserialize_recovery_envelope(
          pcm::app_settings::backupEncryptionRecoveryEnvelope().toStdString());
      workspaceUuid = QString::fromStdString(mDb->get_application_metadata().workspace_uuid);
    } catch (const std::exception &) {
      // Treat metadata access failures like an unavailable encryption key.
    }
    const auto expectedEntry = pcm::backup::workspaceBackupKeychainEntry(workspaceUuid);
    if (!recoveryEnvelope.has_value() || workspaceUuid.isEmpty() ||
        pcm::app_settings::backupEncryptionKeychainEntry() != expectedEntry) {
      mBackupStatusLabel->setVisible(false);
      mBackupProgressBar->setVisible(false);
      mCreateBackupButton->setEnabled(true);
      mValidateBackupButton->setEnabled(true);
      QMessageBox::warning(this, tr("Backup Failed"),
                           tr("Encrypted backup key is unavailable."));
      return;
    }

    mPendingManualBackupDestinationPath = destinationPath;
    mPendingManualBackupEnvelope = *recoveryEnvelope;
    mManualBackupKeyReadInProgress = true;
    mCredentialStore->readWorkspaceMasterKey(workspaceUuid);
    return;
  }

  startBackupWorker(destinationPath);
}

void SettingsDialog::startBackupWorker(
    const QString &destinationPath,
    std::optional<pcm::backup::BackupEncryptionOptions> encryption) {

  auto *thread = new QThread(this);
  auto *worker = new BackupWorker(mDb, destinationPath,
                                  pcm::app_settings::attachmentsStorageRoot(),
                                  std::move(encryption));
  worker->moveToThread(thread);

  connect(thread, &QThread::started, worker, &BackupWorker::run);
  connect(worker, &BackupWorker::finished, this,
          [this, destinationPath](const bool ok, const QString &error) {
            mBackupStatusLabel->setVisible(false);
            mBackupProgressBar->setVisible(false);
            mCreateBackupButton->setEnabled(true);
            mValidateBackupButton->setEnabled(true);
            if (ok) {
              QMessageBox::information(
                  this, tr("Backup Created"),
                  tr("Backup created at:\n%1").arg(destinationPath));
            } else {
              QMessageBox::warning(this, tr("Backup Failed"), error);
            }
          });
  connect(worker, &BackupWorker::finished, thread, &QThread::quit);
  connect(thread, &QThread::finished, worker, &QObject::deleteLater);
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  thread->start();
}

void SettingsDialog::onManualBackupKeyRead(const bool ok,
                                           pcm::backup::MasterKey key,
                                           const QString &error) {
  if (!mManualBackupKeyReadInProgress) {
    clearMasterKey(&key);
    return;
  }

  mManualBackupKeyReadInProgress = false;
  const auto destinationPath = std::move(mPendingManualBackupDestinationPath);
  const auto recoveryEnvelope = std::move(mPendingManualBackupEnvelope);
  mPendingManualBackupEnvelope.reset();
  if (!ok || !recoveryEnvelope.has_value()) {
    clearMasterKey(&key);
    mBackupStatusLabel->setVisible(false);
    mBackupProgressBar->setVisible(false);
    mCreateBackupButton->setEnabled(true);
    mValidateBackupButton->setEnabled(true);
    QMessageBox::warning(this, tr("Backup Failed"),
                         error.isEmpty() ? tr("Encrypted backup key is unavailable.")
                                         : error);
    return;
  }

  pcm::backup::BackupEncryptionOptions encryption{
      .master_key = key,
      .recovery_envelope = std::move(recoveryEnvelope),
  };
  clearMasterKey(&key);
  startBackupWorker(destinationPath, std::move(encryption));
}

void SettingsDialog::enableBackupEncryption() {
  QString password = mBackupEncryptionPasswordEdit->text();
  QString confirmation = mBackupEncryptionConfirmationEdit->text();
  mBackupEncryptionPasswordEdit->clear();
  mBackupEncryptionConfirmationEdit->clear();

  if (password.size() < 12) {
    clearSensitiveText(&password);
    clearSensitiveText(&confirmation);
    QMessageBox::warning(this, tr("Backup Encryption"),
                         tr("The recovery password must contain at least 12 characters."));
    return;
  }
  if (password != confirmation) {
    clearSensitiveText(&password);
    clearSensitiveText(&confirmation);
    QMessageBox::warning(this, tr("Backup Encryption"),
                         tr("Recovery passwords do not match."));
    return;
  }

  pcm::backup::MasterKey key;
  if (sodium_init() < 0) {
    clearSensitiveText(&password);
    clearSensitiveText(&confirmation);
    QMessageBox::warning(this, tr("Backup Encryption"),
                         tr("Backup encryption is unavailable."));
    return;
  }
  randombytes_buf(key.bytes.data(), key.bytes.size());

  auto passwordBytes = password.toUtf8();
  pcm::backup::RecoveryEnvelope recoveryEnvelope;
  const auto envelopeResult = pcm::backup::create_recovery_envelope(
      std::string_view(passwordBytes.constData(),
                       static_cast<std::size_t>(passwordBytes.size())),
      key, &recoveryEnvelope);
  std::fill(passwordBytes.begin(), passwordBytes.end(), '\0');
  passwordBytes.clear();
  clearSensitiveText(&password);
  clearSensitiveText(&confirmation);
  if (!envelopeResult.ok) {
    clearMasterKey(&key);
    QMessageBox::warning(this, tr("Backup Encryption"),
                         tr("Backup encryption is unavailable."));
    return;
  }

  QString workspaceUuid;
  try {
    workspaceUuid = QString::fromStdString(mDb->get_application_metadata().workspace_uuid);
  } catch (const std::exception &) {
    clearMasterKey(&key);
    QMessageBox::warning(this, tr("Backup Encryption"),
                         tr("Backup encryption is unavailable."));
    return;
  }
  if (workspaceUuid.isEmpty()) {
    clearMasterKey(&key);
    QMessageBox::warning(this, tr("Backup Encryption"),
                         tr("Backup encryption is unavailable."));
    return;
  }

  mPendingEncryptionKey = key;
  clearMasterKey(&key);
  mPendingEncryptionWorkspaceUuid = workspaceUuid;
  mPendingEncryptionEnvelope = std::move(recoveryEnvelope);
  mBackupEncryptionEnableInProgress = true;
  mBackupEncryptionEnabledSwitch->setEnabled(false);
  mEnableBackupEncryptionButton->setEnabled(false);
  mCreateBackupButton->setEnabled(false);
  mValidateBackupButton->setEnabled(false);
  mCredentialStore->writeWorkspaceMasterKey(workspaceUuid, *mPendingEncryptionKey);
}

void SettingsDialog::onBackupEncryptionKeyWritten(const bool ok,
                                                   const QString &error) {
  if (!mBackupEncryptionEnableInProgress) {
    return;
  }

  mBackupEncryptionEnableInProgress = false;
  if (mPendingEncryptionKey.has_value()) {
    clearMasterKey(&*mPendingEncryptionKey);
  }
  mPendingEncryptionKey.reset();
  mBackupEncryptionEnabledSwitch->setEnabled(true);
  mCreateBackupButton->setEnabled(true);
  mValidateBackupButton->setEnabled(true);
  if (ok && mPendingEncryptionEnvelope.has_value()) {
    pcm::app_settings::setBackupEncryptionKeychainEntry(
        pcm::backup::workspaceBackupKeychainEntry(mPendingEncryptionWorkspaceUuid));
    pcm::app_settings::setBackupEncryptionRecoveryEnvelope(
        QString::fromStdString(
            pcm::backup::serialize_recovery_envelope(*mPendingEncryptionEnvelope)));
    pcm::app_settings::setBackupEncryptionEnabled(true);
    mPendingEncryptionEnvelope.reset();
    mPendingEncryptionWorkspaceUuid.clear();
    updateBackupEncryptionUi();
    return;
  }

  mPendingEncryptionEnvelope.reset();
  mPendingEncryptionWorkspaceUuid.clear();
  mBackupEncryptionEnabledSwitch->setChecked(false);
  updateBackupEncryptionUi();
  QMessageBox::warning(this, tr("Backup Encryption"),
                       error.isEmpty() ? tr("System keychain is unavailable.") : error);
}

void SettingsDialog::updateBackupEncryptionUi() const {
  const bool needsSetup = mBackupEncryptionEnabledSwitch->isChecked() &&
                          !pcm::app_settings::backupEncryptionEnabled();
  mBackupEncryptionDetails->setVisible(needsSetup);
  mEnableBackupEncryptionButton->setEnabled(
      needsSetup && !mBackupEncryptionEnableInProgress);
}

void SettingsDialog::browseAutoBackupDestination() {
  const auto selected = QFileDialog::getExistingDirectory(
      this, tr("Select Automatic Backup Folder"),
      mAutoBackupDestinationEdit->text());
  if (selected.isEmpty()) {
    return;
  }
  mAutoBackupDestinationEdit->setText(selected);
  pcm::app_settings::setAutoBackupDestination(selected);
}

void SettingsDialog::saveLiveKitSettings() {
  // One guard covers both the read and the save. A failed read skips the
  // save: writing a default-constructed Config over an unreadable file would
  // silently reset the app role and database path stored alongside the URL.
  try {
    auto conf = pcm::config::Config::read_config();
    conf.token_backend_base_url = mLiveKitBaseUrlEdit->text().toStdString();
    pcm::config::Config::save_config(conf);
  } catch (const std::exception &error) {
    qWarning() << "SettingsDialog: failed to save the token backend URL:" << error.what();
    QMessageBox::warning(
        this, tr("Token backend"),
        tr("The token backend URL could not be saved:\n%1").arg(QString::fromUtf8(error.what())));
  }

  // The bearer credential lives in the keychain, independent of Config.yaml,
  // so it is still saved when the config write above failed.
  const auto credential = mLiveKitBearerCredentialEdit->text();
  if (!credential.isEmpty()) {
    mTokenBackendCredentialStore->writeBearerCredential(credential);
  }
}

void SettingsDialog::onBearerCredentialRead(const bool ok, const QString &credential,
                                            const QString & /*error*/) {
  // `credential` is the real secret; it must never be placed into any widget
  // or otherwise surfaced -- only whether it is non-empty.
  mLiveKitBearerCredentialStatusLabel->setText(ok && !credential.isEmpty()
                                                   ? tr("Credentials are saved")
                                                   : tr("No credentials saved yet"));
}

void SettingsDialog::validateBackup() {
  const auto defaultDir =
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  const auto backupPath = QFileDialog::getOpenFileName(
      this, tr("Validate Backup"), defaultDir,
      tr("Sessio Backup (*.psybackup)"));
  if (backupPath.isEmpty()) {
    return;
  }

  mCreateBackupButton->setEnabled(false);
  mValidateBackupButton->setEnabled(false);
  mBackupStatusLabel->setText(tr("Validating backup…"));
  mBackupStatusLabel->setVisible(true);
  mBackupProgressBar->setVisible(true);

  auto *thread = new QThread(this);
  auto worker = std::make_unique<ValidateWorker>(backupPath);
  auto *workerObject = worker.get();
  workerObject->moveToThread(thread);

  connect(thread, &QThread::started, workerObject, &ValidateWorker::run);
  connect(workerObject, &ValidateWorker::finished, this,
          [this](const bool ok, const QStringList &errors) {
            mBackupStatusLabel->setVisible(false);
            mBackupProgressBar->setVisible(false);
            mCreateBackupButton->setEnabled(true);
            mValidateBackupButton->setEnabled(true);
            if (ok) {
              QMessageBox::information(this, tr("Backup Valid"),
                                       tr("The backup is valid."));
            } else {
              QString message;
              if (errors.size() > 10) {
                message = errors.mid(0, 10).join('\n') +
                          tr("\n... and %1 more").arg(errors.size() - 10);
              } else {
                message = errors.join('\n');
              }
              QMessageBox::warning(this, tr("Backup Invalid"), message);
            }
          });
  connect(workerObject, &ValidateWorker::finished, thread, &QThread::quit);
  connect(thread, &QThread::finished, workerObject, &QObject::deleteLater);
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  worker.release();
  thread->start();
}

void SettingsDialog::restoreBackup() {
  const auto defaultDir =
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  const auto backupPath = QFileDialog::getOpenFileName(
      this, tr("Restore Backup"), defaultDir,
      tr("Sessio Backup (*.psybackup)"));
  if (backupPath.isEmpty()) {
    return;
  }

  std::optional<std::string> recoveryPassword;
  if (pcm::backup::detect_backup_container(backupPath.toStdString()) ==
      pcm::backup::BackupContainerKind::Encrypted) {
    bool accepted = false;
    QString password = QInputDialog::getText(
        this, tr("Encrypted Backup"),
        tr("This backup is encrypted. Enter its recovery password to validate it. "
           "You will be asked again after the application restarts to complete "
           "the restore."),
        QLineEdit::Password, {}, &accepted);
    if (!accepted) {
      clearSensitiveText(&password);
      return;
    }
    auto passwordBytes = password.toUtf8();
    recoveryPassword = std::string(
        passwordBytes.constData(), static_cast<std::size_t>(passwordBytes.size()));
    std::fill(passwordBytes.begin(), passwordBytes.end(), '\0');
    clearSensitiveText(&password);
  }

  mCreateBackupButton->setEnabled(false);
  mValidateBackupButton->setEnabled(false);
  mRestoreBackupButton->setEnabled(false);
  mBackupStatusLabel->setText(tr("Checking backup…"));
  mBackupStatusLabel->setVisible(true);
  mBackupProgressBar->setVisible(true);

  auto *thread = new QThread(this);
  auto worker = std::make_unique<ValidateWorker>(backupPath, std::move(recoveryPassword));
  auto *workerObject = worker.get();
  workerObject->moveToThread(thread);

  connect(thread, &QThread::started, workerObject, &ValidateWorker::run);
  connect(workerObject, &ValidateWorker::finished, this,
          [this, backupPath](const bool ok, const QStringList &errors) {
            mBackupStatusLabel->setVisible(false);
            mBackupProgressBar->setVisible(false);
            mCreateBackupButton->setEnabled(true);
            mValidateBackupButton->setEnabled(true);
            mRestoreBackupButton->setEnabled(true);
            if (!ok) {
              QString message;
              if (errors.size() > 10) {
                message = errors.mid(0, 10).join('\n') +
                          tr("\n... and %1 more").arg(errors.size() - 10);
              } else {
                message = errors.join('\n');
              }
              QMessageBox::warning(this, tr("Backup Invalid"), message);
              return;
            }

            const auto confirmation = QMessageBox::warning(
                this, tr("Restore Backup"),
                tr("This will replace all current data (clients, events, "
                   "notes, and attachments) with the contents of this "
                   "backup. Your current data will be kept as a protective "
                   "copy, but the application must restart to complete the "
                   "restore. Continue?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (confirmation != QMessageBox::Yes) {
              return;
            }

            const auto markerPath = Poco::Path(mConfig.config_pth.value())
                                        .makeParent()
                                        .append("pending-restore.json")
                                        .toString();
            if (!pcm::backup::write_pending_restore_marker(
                    markerPath, backupPath.toStdString())) {
              QMessageBox::warning(
                  this, tr("Restore Failed"),
                  tr("Could not stage the restore. Check that there is "
                     "enough disk space and try again."));
              return;
            }

            QMessageBox::information(
                this, tr("Restore Staged"),
                tr("Sessio will now close. Restart it to "
                   "complete the restore."));
            QApplication::quit();
          });
  connect(workerObject, &ValidateWorker::finished, thread, &QThread::quit);
  connect(thread, &QThread::finished, workerObject, &QObject::deleteLater);
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  worker.release();
  thread->start();
}

#include "settings_dialog.moc"
