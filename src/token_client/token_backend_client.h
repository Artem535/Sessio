#pragma once

#include "token_result.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QList>
#include <QPair>
#include <QString>

class QNetworkReply;

namespace pcm::tokenclient {

class TokenBackendClient final : public QObject {
  Q_OBJECT

public:
  explicit TokenBackendClient(QString baseUrl, QObject *parent = nullptr);

  // Retargets every later request at a different token backend. The base URL
  // is read fresh on each request (never cached elsewhere), so this is safe to
  // call at any time; requests already in flight finish against the old URL.
  void setBaseUrl(const QString &baseUrl);
  [[nodiscard]] QString baseUrl() const { return mBaseUrl; }

  void requestSpecialistToken(const QString &bearerCredential, const QString &meetingRef,
                              const QString &displayName = {});
  void requestClientToken(const QString &invitationCode, const QString &passcode,
                          const QString &displayName = {});
  void requestCreateMeeting(const QString &bearerCredential, const QString &scheduledStartIso,
                            const QString &scheduledEndIso);
  void requestInvalidateMeeting(const QString &bearerCredential, const QString &meetingRef);

  // --- Recurring schedule series ---
  // Every call is asynchronous (never pumps the event loop), has a bounded
  // timeout and reports through its own signal with a result that names the
  // series UUID, so concurrent series cannot be confused with one another.
  static constexpr int kDefaultScheduleRequestTimeoutMs = 15000;
  void setScheduleRequestTimeoutMs(int timeoutMs) { mScheduleTimeoutMs = timeoutMs; }
  [[nodiscard]] int scheduleRequestTimeoutMs() const { return mScheduleTimeoutMs; }

  void requestScheduleCapabilities(const QString &bearerCredential);
  // `body` is sent verbatim so a retry replays identical bytes.
  void putSchedule(const QString &bearerCredential, const QString &seriesUid, qint64 revision,
                   const QByteArray &body);
  void getSchedule(const QString &bearerCredential, const QString &seriesUid);
  void requestSeriesInvitation(const QString &bearerCredential, const QString &seriesUid,
                               const QString &idempotencyKey, bool reissue);
  void requestSeriesRevoke(const QString &bearerCredential, const QString &seriesUid);

signals:
  void tokenReceived(pcm::tokenclient::TokenResult result);
  void tokenRequestFailed(QString reason);
  void meetingCreated(pcm::tokenclient::MeetingCreateResult result);
  void meetingCreateFailed(QString reason);
  void meetingInvalidated();
  void meetingInvalidateFailed(QString reason);
  void scheduleCapabilitiesFinished(pcm::tokenclient::ScheduleHttpResult result);
  void schedulePutFinished(pcm::tokenclient::ScheduleHttpResult result);
  void scheduleGetFinished(pcm::tokenclient::ScheduleHttpResult result);
  void seriesInvitationFinished(pcm::tokenclient::ScheduleHttpResult result);
  void seriesRevokeFinished(pcm::tokenclient::ScheduleHttpResult result);

private:
  [[nodiscard]] QNetworkReply *sendPost(const QString &path, const QByteArray &body,
                                        const QString &bearerCredential);
  void post(const QString &path, const QByteArray &body, const QString &bearerCredential);
  // Sends one schedule call and invokes `done` exactly once with its outcome.
  template <class Done>
  void sendSchedule(const QByteArray &verb, const QString &path, const QByteArray &body,
                    const QString &bearerCredential,
                    const QList<QPair<QByteArray, QByteArray>> &headers, const QString &seriesUid,
                    qint64 revision, Done done);

  QNetworkAccessManager mNetworkManager;
  QString mBaseUrl;
  int mScheduleTimeoutMs = kDefaultScheduleRequestTimeoutMs;
};

} // namespace pcm::tokenclient

Q_DECLARE_METATYPE(pcm::tokenclient::TokenResult)
Q_DECLARE_METATYPE(pcm::tokenclient::MeetingCreateResult)
Q_DECLARE_METATYPE(pcm::tokenclient::ScheduleHttpResult)
