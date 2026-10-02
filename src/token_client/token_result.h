#pragma once

#include <QByteArray>
#include <QString>

namespace pcm::tokenclient {

struct TokenResult {
  QString endpointUrl;
  QString roomName;
  QString token;
  qint64 expiresAt = 0;
};

// Outcome of one schedule-series HTTP call, correlated by the series UUID (and
// the revision for PUT) it was issued for. Never carries the credential. The
// body of a successful invitation reply contains secrets: keep it out of logs.
struct ScheduleHttpResult {
  QString seriesUid;        // empty for the capabilities probe
  qint64 revision = 0;      // revision sent by PUT; 0 for other calls
  int httpStatus = 0;       // 0: no HTTP response (offline, refused, TLS, timeout)
  bool timedOut = false;    // request exceeded the client timeout
  QString errorCode;        // server "error" field of a non-2xx reply
  QString reason;           // server "reason" field, when present
  int retryAfterSeconds = 0; // Retry-After header, clamped to [0, 3600]
  QByteArray body;

  [[nodiscard]] bool ok() const { return httpStatus >= 200 && httpStatus < 300; }
};

struct MeetingCreateResult {
  QString meetingRef;
  QString invitationUrl;
  QString passcode;
};

} // namespace pcm::tokenclient
