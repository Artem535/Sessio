#include "meeting_utils.h"

#include "app_settings.h"
#include "sensitive_clipboard.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QLocale>
#include <QMessageBox>
#include <QObject>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>

namespace pcm::meeting {

bool isValidMeetingUrl(const QString &url) {
  const auto trimmedUrl = url.trimmed();
  if (trimmedUrl.isEmpty()) {
    return false;
  }

  const QUrl parsedUrl(trimmedUrl);
  return parsedUrl.isValid() &&
         (parsedUrl.scheme() == QStringLiteral("http") ||
          parsedUrl.scheme() == QStringLiteral("https"));
}

bool isValidInvitationUrl(const QString &url) {
  if (isValidMeetingUrl(url)) {
    return true;
  }

  const QUrl parsedUrl(url.trimmed());
  if (!parsedUrl.isValid() || parsedUrl.scheme() != QStringLiteral("sessio") ||
      parsedUrl.host() != QStringLiteral("join")) {
    return false;
  }
  const QUrlQuery query(parsedUrl);
  return !query.queryItemValue(QStringLiteral("code")).isEmpty() &&
         !query.queryItemValue(QStringLiteral("passcode")).isEmpty();
}

void openMeetingUrl(const QString &url, QWidget *parent) {
  const auto trimmedUrl = url.trimmed();
  if (!isValidMeetingUrl(trimmedUrl)) {
    QMessageBox::warning(parent, QObject::tr(": ERROR_TITLE"),
                         QObject::tr("Enter a valid http or https meeting link."));
    return;
  }

  QDesktopServices::openUrl(QUrl(trimmedUrl));
}

void copyMeetingUrl(const QString &url) {
  pcm::clipboard::copySensitiveText(url.trimmed());
}

QString buildMeetingInviteText(const QString &meetingUrl, const QString &clientName,
                               const qint64 startDateTimeUtcMs) {
  const auto startDateTime =
      QDateTime::fromMSecsSinceEpoch(startDateTimeUtcMs, QTimeZone::UTC).toLocalTime();
  const auto date = QLocale().toString(startDateTime.date(), QLocale::ShortFormat);
  const auto time = QLocale().toString(startDateTime.time(), QLocale::ShortFormat);

  auto text = pcm::app_settings::meetingInviteTemplate();
  text.replace(QStringLiteral("{client_name}"), clientName.trimmed());
  text.replace(QStringLiteral("{date}"), date);
  text.replace(QStringLiteral("{time}"), time);
  text.replace(QStringLiteral("{meeting_url}"), meetingUrl.trimmed());
  return text;
}

void copyMeetingInvite(const QString &meetingUrl, const QString &clientName,
                       const qint64 startDateTimeUtcMs) {
  pcm::clipboard::copySensitiveText(
      buildMeetingInviteText(meetingUrl, clientName, startDateTimeUtcMs));
}

} // namespace pcm::meeting
