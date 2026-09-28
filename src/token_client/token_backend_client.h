#pragma once

#include "token_result.h"

#include <QNetworkAccessManager>
#include <QObject>
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

  void requestSpecialistToken(const QString &bearerCredential, const QString &meetingRef);
  void requestClientToken(const QString &invitationCode, const QString &passcode);
  void requestCreateMeeting(const QString &bearerCredential, const QString &scheduledStartIso,
                            const QString &scheduledEndIso);
  void requestInvalidateMeeting(const QString &bearerCredential, const QString &meetingRef);

signals:
  void tokenReceived(pcm::tokenclient::TokenResult result);
  void tokenRequestFailed(QString reason);
  void meetingCreated(pcm::tokenclient::MeetingCreateResult result);
  void meetingCreateFailed(QString reason);
  void meetingInvalidated();
  void meetingInvalidateFailed(QString reason);

private:
  [[nodiscard]] QNetworkReply *sendPost(const QString &path, const QByteArray &body,
                                        const QString &bearerCredential);
  void post(const QString &path, const QByteArray &body, const QString &bearerCredential);

  QNetworkAccessManager mNetworkManager;
  QString mBaseUrl;
};

} // namespace pcm::tokenclient

Q_DECLARE_METATYPE(pcm::tokenclient::TokenResult)
Q_DECLARE_METATYPE(pcm::tokenclient::MeetingCreateResult)
