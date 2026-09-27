#pragma once

#include "token_result.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

namespace pcm::tokenclient {

class TokenBackendClient final : public QObject {
  Q_OBJECT

public:
  explicit TokenBackendClient(QString baseUrl, QObject *parent = nullptr);

  void requestSpecialistToken(const QString &bearerCredential, const QString &meetingRef);
  void requestClientToken(const QString &invitationCode, const QString &passcode);

signals:
  void tokenReceived(pcm::tokenclient::TokenResult result);
  void tokenRequestFailed(QString reason);

private:
  void post(const QString &path, const QByteArray &body, const QString &bearerCredential);

  QNetworkAccessManager mNetworkManager;
  QString mBaseUrl;
};

} // namespace pcm::tokenclient

Q_DECLARE_METATYPE(pcm::tokenclient::TokenResult)
