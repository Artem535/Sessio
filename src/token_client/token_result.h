#pragma once

#include <QString>

namespace pcm::tokenclient {

struct TokenResult {
  QString endpointUrl;
  QString roomName;
  QString token;
  qint64 expiresAt = 0;
};

struct MeetingCreateResult {
  QString meetingRef;
  QString invitationUrl;
  QString passcode;
};

} // namespace pcm::tokenclient
