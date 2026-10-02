#pragma once

#include <QString>

class QWidget;

namespace pcm::meeting {

bool isValidMeetingUrl(const QString &url);
bool isValidInvitationUrl(const QString &url);
void openMeetingUrl(const QString &url, QWidget *parent = nullptr);
void copyMeetingUrl(const QString &url);
QString buildMeetingInviteText(const QString &meetingUrl, const QString &clientName,
                               qint64 startDateTimeUtcMs);
void copyMeetingInvite(const QString &meetingUrl, const QString &clientName,
                       qint64 startDateTimeUtcMs);

} // namespace pcm::meeting
