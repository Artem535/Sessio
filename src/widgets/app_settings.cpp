#include "app_settings.h"

#include <QColor>
#include <QDir>
#include <QObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTime>

namespace {
constexpr auto kConfirmEventDeletionKey = "event/confirmDeletion";
constexpr auto kPreventEventOverlapsKey = "event/preventOverlaps";
constexpr auto kShowStatusBarMessagesKey = "ui/showStatusBarMessages";
constexpr auto kNotificationsEnabledKey = "notification/enabled";
constexpr auto kNotificationLeadMinutesKey = "notification/leadMinutes";
constexpr auto kNotificationPrivacyModeKey = "notification/privacyMode";
constexpr auto kAppLockTimeoutMinutesKey = "privacy/appLockTimeoutMinutes";
constexpr auto kClearSensitiveClipboardKey = "privacy/clearSensitiveClipboard";
constexpr auto kSensitiveClipboardClearDelaySecondsKey =
    "privacy/sensitiveClipboardClearDelaySeconds";
constexpr auto kLanguageCodeKey = "ui/language";
constexpr auto kWorkEventColorKey = "timeline/workEventColor";
constexpr auto kPersonalEventColorKey = "timeline/personalEventColor";
constexpr auto kDefaultWorkEventCostKey = "event/defaultWorkEventCost";
constexpr auto kWorkDayStartKey = "event/workDayStart";
constexpr auto kWorkDayEndKey = "event/workDayEnd";
constexpr auto kDefaultSessionDurationMinutesKey = "event/defaultSessionDurationMinutes";
constexpr auto kDefaultBufferBeforeMinutesKey = "event/defaultBufferBeforeMinutes";
constexpr auto kDefaultBufferAfterMinutesKey = "event/defaultBufferAfterMinutes";
constexpr auto kMeetingInviteTemplateKey = "online/meetingInviteTemplate";
constexpr auto kCurrencyCodeKey = "ui/currency";
constexpr auto kAutoBackupEnabledKey = "backup/autoEnabled";
constexpr auto kAutoBackupIntervalDaysKey = "backup/autoIntervalDays";
constexpr auto kAutoBackupKeepCountKey = "backup/autoKeepCount";
constexpr auto kAutoBackupDestinationKey = "backup/autoDestination";
constexpr auto kAutoBackupLastRunAtMsKey = "backup/autoLastRunAtMs";
constexpr auto kBackupEncryptionEnabledKey = "backup/encryptionEnabled";
constexpr auto kBackupEncryptionKeychainEntryKey = "backup/keychainEntry";
constexpr auto kBackupEncryptionRecoveryEnvelopeKey = "backup/recoveryEnvelope";

QColor defaultWorkEventColor() {
  return QColor(37, 99, 235);
}

QColor defaultPersonalEventColor() {
  return QColor(126, 34, 206);
}

QColor legacyDefaultWorkEventColor() {
  return QColor(173, 216, 230);
}

QColor legacyDefaultPersonalEventColor() {
  return QColor(255, 182, 193);
}

double defaultWorkEventCostValue() {
  return 2500.0;
}

QTime defaultWorkDayStartValue() {
  return QTime(9, 0);
}

QTime defaultWorkDayEndValue() {
  return QTime(18, 0);
}

int defaultSessionDurationMinutesValue() {
  return 60;
}

int defaultBufferBeforeMinutesValue() { return 0; }
int defaultBufferAfterMinutesValue() { return 0; }

int defaultNotificationLeadMinutesValue() {
  return 30;
}

pcm::NotificationPrivacyMode defaultNotificationPrivacyModeValue() {
  return pcm::NotificationPrivacyMode::Hidden;
}

int defaultAppLockTimeoutMinutesValue() { return 10; }
int defaultSensitiveClipboardClearDelaySecondsValue() { return 60; }

QString defaultMeetingInviteTemplateValue() {
  return QObject::tr("Hello, {client_name}!\n\n"
                     "We meet on {date} at {time}.\n\n"
                     "Connection link:\n"
                     "{meeting_url}\n\n"
                     "See you!");
}

int defaultAutoBackupIntervalDaysValue() {
  return 7;
}

int defaultAutoBackupKeepCountValue() {
  return 7;
}

QString defaultAutoBackupDestinationValue() {
  const auto basePath =
      QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  return QDir(basePath).filePath("backups");
}
} // namespace

namespace pcm::app_settings {
bool transcriptionEnabled() {
  return QSettings().value("transcription/enabled", true).toBool();
}

void setTranscriptionEnabled(bool enabled) {
  QSettings().setValue("transcription/enabled", enabled);
}

QString callDisplayName() {
  return QSettings().value("calls/displayName").toString().trimmed().left(64);
}

void setCallDisplayName(const QString &name) {
  QSettings().setValue("calls/displayName", name.trimmed().left(64));
}

bool confirmEventDeletion() {
  QSettings settings;
  return settings.value(kConfirmEventDeletionKey, true).toBool();
}

void setConfirmEventDeletion(const bool enabled) {
  QSettings settings;
  settings.setValue(kConfirmEventDeletionKey, enabled);
}

bool preventEventOverlaps() {
  QSettings settings;
  return settings.value(kPreventEventOverlapsKey, true).toBool();
}

void setPreventEventOverlaps(const bool enabled) {
  QSettings settings;
  settings.setValue(kPreventEventOverlapsKey, enabled);
}

bool showStatusBarMessages() {
  QSettings settings;
  return settings.value(kShowStatusBarMessagesKey, true).toBool();
}

void setShowStatusBarMessages(const bool enabled) {
  QSettings settings;
  settings.setValue(kShowStatusBarMessagesKey, enabled);
}

bool notificationsEnabled() {
  QSettings settings;
  return settings.value(kNotificationsEnabledKey, true).toBool();
}

void setNotificationsEnabled(const bool enabled) {
  QSettings settings;
  settings.setValue(kNotificationsEnabledKey, enabled);
}

int notificationLeadMinutes() {
  QSettings settings;
  return settings.value(kNotificationLeadMinutesKey,
                        defaultNotificationLeadMinutesValue())
      .toInt();
}

void setNotificationLeadMinutes(const int minutes) {
  QSettings settings;
  settings.setValue(kNotificationLeadMinutesKey, minutes);
}

NotificationPrivacyMode notificationPrivacyMode() {
  QSettings settings;
  const auto stored = settings.value(
      kNotificationPrivacyModeKey,
      static_cast<int>(defaultNotificationPrivacyModeValue()));
  const auto value = stored.toInt();
  switch (value) {
  case static_cast<int>(NotificationPrivacyMode::Full):
    return NotificationPrivacyMode::Full;
  case static_cast<int>(NotificationPrivacyMode::Minimal):
    return NotificationPrivacyMode::Minimal;
  case static_cast<int>(NotificationPrivacyMode::Hidden):
  default:
    return NotificationPrivacyMode::Hidden;
  }
}

void setNotificationPrivacyMode(const NotificationPrivacyMode mode) {
  QSettings settings;
  settings.setValue(kNotificationPrivacyModeKey, static_cast<int>(mode));
}

int appLockTimeoutMinutes() {
  QSettings settings;
  return settings.value(kAppLockTimeoutMinutesKey,
                        defaultAppLockTimeoutMinutesValue()).toInt();
}

void setAppLockTimeoutMinutes(const int minutes) {
  QSettings settings;
  settings.setValue(kAppLockTimeoutMinutesKey, minutes);
}

bool clearSensitiveClipboard() {
  QSettings settings;
  return settings.value(kClearSensitiveClipboardKey, true).toBool();
}

void setClearSensitiveClipboard(const bool enabled) {
  QSettings settings;
  settings.setValue(kClearSensitiveClipboardKey, enabled);
}

int sensitiveClipboardClearDelaySeconds() {
  QSettings settings;
  return settings.value(kSensitiveClipboardClearDelaySecondsKey,
                        defaultSensitiveClipboardClearDelaySecondsValue()).toInt();
}

void setSensitiveClipboardClearDelaySeconds(const int seconds) {
  QSettings settings;
  settings.setValue(kSensitiveClipboardClearDelaySecondsKey, seconds);
}

QString languageCode() {
  QSettings settings;
  return settings.value(kLanguageCodeKey, QStringLiteral("system")).toString();
}

void setLanguageCode(const QString &languageCode) {
  QSettings settings;
  settings.setValue(kLanguageCodeKey, languageCode);
}

QColor workEventColor() {
  QSettings settings;
  const auto color = settings.value(kWorkEventColorKey, defaultWorkEventColor()).value<QColor>();
  return color == legacyDefaultWorkEventColor() ? defaultWorkEventColor() : color;
}

void setWorkEventColor(const QColor &color) {
  QSettings settings;
  settings.setValue(kWorkEventColorKey, color);
}

QColor personalEventColor() {
  QSettings settings;
  const auto color =
      settings.value(kPersonalEventColorKey, defaultPersonalEventColor()).value<QColor>();
  return color == legacyDefaultPersonalEventColor() ? defaultPersonalEventColor() : color;
}

void setPersonalEventColor(const QColor &color) {
  QSettings settings;
  settings.setValue(kPersonalEventColorKey, color);
}

double defaultWorkEventCost() {
  QSettings settings;
  return settings.value(kDefaultWorkEventCostKey, defaultWorkEventCostValue())
      .toDouble();
}

void setDefaultWorkEventCost(const double cost) {
  QSettings settings;
  settings.setValue(kDefaultWorkEventCostKey, cost);
}

QTime workDayStart() {
  QSettings settings;
  return settings.value(kWorkDayStartKey, defaultWorkDayStartValue()).toTime();
}

void setWorkDayStart(const QTime &time) {
  QSettings settings;
  settings.setValue(kWorkDayStartKey, time);
}

QTime workDayEnd() {
  QSettings settings;
  return settings.value(kWorkDayEndKey, defaultWorkDayEndValue()).toTime();
}

void setWorkDayEnd(const QTime &time) {
  QSettings settings;
  settings.setValue(kWorkDayEndKey, time);
}

int defaultSessionDurationMinutes() {
  QSettings settings;
  return settings
      .value(kDefaultSessionDurationMinutesKey, defaultSessionDurationMinutesValue())
      .toInt();
}

void setDefaultSessionDurationMinutes(const int minutes) {
  QSettings settings;
  settings.setValue(kDefaultSessionDurationMinutesKey, minutes);
}

int defaultBufferBeforeMinutes() {
  QSettings settings;
  return settings.value(kDefaultBufferBeforeMinutesKey,
                        defaultBufferBeforeMinutesValue()).toInt();
}

void setDefaultBufferBeforeMinutes(const int minutes) {
  QSettings settings;
  settings.setValue(kDefaultBufferBeforeMinutesKey, minutes);
}

int defaultBufferAfterMinutes() {
  QSettings settings;
  return settings.value(kDefaultBufferAfterMinutesKey,
                        defaultBufferAfterMinutesValue()).toInt();
}

void setDefaultBufferAfterMinutes(const int minutes) {
  QSettings settings;
  settings.setValue(kDefaultBufferAfterMinutesKey, minutes);
}

QString meetingInviteTemplate() {
  QSettings settings;
  return settings.value(kMeetingInviteTemplateKey,
                        defaultMeetingInviteTemplateValue())
      .toString();
}

void setMeetingInviteTemplate(const QString &templateText) {
  QSettings settings;
  settings.setValue(kMeetingInviteTemplateKey, templateText);
}

QString currencyCode() {
  QSettings settings;
  return settings.value(kCurrencyCodeKey, QStringLiteral("RUB")).toString();
}

void setCurrencyCode(const QString &code) {
  QSettings settings;
  settings.setValue(kCurrencyCodeKey, code);
}

QString currencySymbol() {
  const auto code = currencyCode();
  if (code == QStringLiteral("USD")) {
    return QStringLiteral("$");
  }
  if (code == QStringLiteral("EUR")) {
    return QStringLiteral("€");
  }
  if (code == QStringLiteral("GBP")) {
    return QStringLiteral("£");
  }
  return QStringLiteral("₽");
}

QString attachmentsStorageRoot() {
  const auto basePath =
      QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  return QDir(basePath).filePath("storage/notes");
}

bool autoBackupEnabled() {
  QSettings settings;
  return settings.value(kAutoBackupEnabledKey, true).toBool();
}

void setAutoBackupEnabled(const bool enabled) {
  QSettings settings;
  settings.setValue(kAutoBackupEnabledKey, enabled);
}

int autoBackupIntervalDays() {
  QSettings settings;
  return settings
      .value(kAutoBackupIntervalDaysKey, defaultAutoBackupIntervalDaysValue())
      .toInt();
}

void setAutoBackupIntervalDays(const int days) {
  QSettings settings;
  settings.setValue(kAutoBackupIntervalDaysKey, days);
}

int autoBackupKeepCount() {
  QSettings settings;
  return settings.value(kAutoBackupKeepCountKey, defaultAutoBackupKeepCountValue())
      .toInt();
}

void setAutoBackupKeepCount(const int count) {
  QSettings settings;
  settings.setValue(kAutoBackupKeepCountKey, count);
}

QString autoBackupDestination() {
  QSettings settings;
  return settings
      .value(kAutoBackupDestinationKey, defaultAutoBackupDestinationValue())
      .toString();
}

void setAutoBackupDestination(const QString &path) {
  QSettings settings;
  settings.setValue(kAutoBackupDestinationKey, path);
}

qint64 autoBackupLastRunAtMs() {
  QSettings settings;
  return settings.value(kAutoBackupLastRunAtMsKey, static_cast<qint64>(0))
      .toLongLong();
}

void setAutoBackupLastRunAtMs(const qint64 ms) {
  QSettings settings;
  settings.setValue(kAutoBackupLastRunAtMsKey, ms);
}

bool backupEncryptionEnabled() {
  QSettings settings;
  return settings.value(kBackupEncryptionEnabledKey, false).toBool();
}

void setBackupEncryptionEnabled(const bool enabled) {
  QSettings settings;
  settings.setValue(kBackupEncryptionEnabledKey, enabled);
}

QString backupEncryptionKeychainEntry() {
  QSettings settings;
  return settings.value(kBackupEncryptionKeychainEntryKey).toString();
}

void setBackupEncryptionKeychainEntry(const QString &entry) {
  QSettings settings;
  settings.setValue(kBackupEncryptionKeychainEntryKey, entry);
}

QString backupEncryptionRecoveryEnvelope() {
  QSettings settings;
  return settings.value(kBackupEncryptionRecoveryEnvelopeKey).toString();
}

void setBackupEncryptionRecoveryEnvelope(const QString &envelope) {
  QSettings settings;
  if (envelope.isEmpty()) {
    settings.remove(kBackupEncryptionRecoveryEnvelopeKey);
    return;
  }
  settings.setValue(kBackupEncryptionRecoveryEnvelopeKey, envelope);
}

} // namespace pcm::app_settings
