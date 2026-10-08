#pragma once

#include <QColor>
#include <QString>
#include <QTime>

#include "notification_text.h"

namespace pcm::app_settings {
bool transcriptionEnabled();
void setTranscriptionEnabled(bool enabled);
QString callDisplayName();
void setCallDisplayName(const QString &name);

bool confirmEventDeletion();
void setConfirmEventDeletion(bool enabled);
bool preventEventOverlaps();
void setPreventEventOverlaps(bool enabled);

bool showStatusBarMessages();
void setShowStatusBarMessages(bool enabled);
bool notificationsEnabled();
void setNotificationsEnabled(bool enabled);
int notificationLeadMinutes();
void setNotificationLeadMinutes(int minutes);
NotificationPrivacyMode notificationPrivacyMode();
void setNotificationPrivacyMode(NotificationPrivacyMode mode);

int appLockTimeoutMinutes();
void setAppLockTimeoutMinutes(int minutes);
bool clearSensitiveClipboard();
void setClearSensitiveClipboard(bool enabled);
int sensitiveClipboardClearDelaySeconds();
void setSensitiveClipboardClearDelaySeconds(int seconds);

QString languageCode();
void setLanguageCode(const QString &languageCode);

QColor workEventColor();
void setWorkEventColor(const QColor &color);

QColor personalEventColor();
void setPersonalEventColor(const QColor &color);

double defaultWorkEventCost();
void setDefaultWorkEventCost(double cost);

QTime workDayStart();
void setWorkDayStart(const QTime &time);

QTime workDayEnd();
void setWorkDayEnd(const QTime &time);

int defaultSessionDurationMinutes();
void setDefaultSessionDurationMinutes(int minutes);
int defaultBufferBeforeMinutes();
void setDefaultBufferBeforeMinutes(int minutes);
int defaultBufferAfterMinutes();
void setDefaultBufferAfterMinutes(int minutes);

QString meetingInviteTemplate();
void setMeetingInviteTemplate(const QString &templateText);

QString currencyCode();
void setCurrencyCode(const QString &code);
QString currencySymbol();

QString attachmentsStorageRoot();

bool autoBackupEnabled();
void setAutoBackupEnabled(bool enabled);
int autoBackupIntervalDays();
void setAutoBackupIntervalDays(int days);
int autoBackupKeepCount();
void setAutoBackupKeepCount(int count);
QString autoBackupDestination();
void setAutoBackupDestination(const QString &path);
qint64 autoBackupLastRunAtMs();
void setAutoBackupLastRunAtMs(qint64 ms);

bool backupEncryptionEnabled();
void setBackupEncryptionEnabled(bool enabled);
QString backupEncryptionKeychainEntry();
void setBackupEncryptionKeychainEntry(const QString &entry);
QString backupEncryptionRecoveryEnvelope();
void setBackupEncryptionRecoveryEnvelope(const QString &envelope);

} // namespace pcm::app_settings
