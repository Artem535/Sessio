#pragma once

#include <QColor>
#include <QString>
#include <QTime>

#include "notification_text.h"
#include "transcription_tuning.h"

namespace pcm::app_settings {
bool transcriptionEnabled();
void setTranscriptionEnabled(bool enabled);
bool transcriptionAutoStart();
void setTranscriptionAutoStart(bool enabled);
bool prompterAutoStart();
void setPrompterAutoStart(bool enabled);
QString prompterModel();
void setPrompterModel(const QString &model);
// "auto" (GPU, CPU fallback), "gpu" or "cpu"; applies at the next model load.
// Cloud LLM (an OpenAI-compatible endpoint chosen by the user). Off by default.
// The API key is not here: it lives in the system keychain (CloudKeyStore).
// A feature is "agreements", "live_points" or "summary"; its engine is "local"
// or "cloud". Cloud is used only while consent is valid for the current URL.
QString cloudBaseUrl();
void setCloudBaseUrl(const QString &url); // a different URL revokes consent
QString cloudModel();
void setCloudModel(const QString &model);
int cloudContextTokens();
void setCloudContextTokens(int tokens);
bool cloudConsentValid();
void acceptCloudConsent(); // binds consent to the current base URL
void revokeCloudConsent(); // every feature goes back to local
bool cloudPseudonymize();  // default true
void setCloudPseudonymize(bool enabled);
// The API key as read from the keychain, kept in memory only for this run.
QString cloudApiKeyCached();
void setCloudApiKeyCached(const QString &key);
QString featureEngine(const QString &feature);
void setFeatureEngine(const QString &feature, const QString &engine);

QString prompterAcceleration();
void setPrompterAcceleration(const QString &mode);
// Speech detection and recogniser parameters; always returned clamped to their valid ranges.
pcm::transcription::TranscriptionTuning transcriptionTuning();
void setTranscriptionTuning(const pcm::transcription::TranscriptionTuning &tuning);
QString callDisplayName();
void setCallDisplayName(const QString &name);
QString externalJitsiServerUrl();
void setExternalJitsiServerUrl(const QString &url);

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
