#include "token_backend_client.h"
#include "token_response_parser.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace pcm::tokenclient {

TokenBackendClient::TokenBackendClient(QString baseUrl, QObject *parent)
    : QObject(parent), mBaseUrl(std::move(baseUrl)) {
  qRegisterMetaType<TokenResult>();
}

void TokenBackendClient::requestSpecialistToken(const QString &bearerCredential,
                                                const QString &meetingRef) {
  post(QStringLiteral("/v1/meetings/%1/specialist-token").arg(meetingRef), QByteArray(),
       bearerCredential);
}

void TokenBackendClient::requestClientToken(const QString &invitationCode,
                                            const QString &passcode) {
  QJsonObject bodyObject;
  bodyObject["passcode"] = passcode;
  const QByteArray body = QJsonDocument(bodyObject).toJson(QJsonDocument::Compact);
  post(QStringLiteral("/v1/invitations/%1/client-token").arg(invitationCode), body, QString());
}

void TokenBackendClient::post(const QString &path, const QByteArray &body,
                              const QString &bearerCredential) {
  QNetworkRequest request(QUrl(mBaseUrl + path));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  if (!bearerCredential.isEmpty()) {
    request.setRawHeader("Authorization", "Bearer " + bearerCredential.toUtf8());
  }

  auto *reply = mNetworkManager.post(request, body);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();
    const auto responseBody = reply->readAll();
    const auto httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError || httpStatus != 200) {
      emit tokenRequestFailed(parseErrorMessage(responseBody));
      return;
    }

    const auto result = parseTokenResponse(responseBody);
    if (!result.has_value()) {
      emit tokenRequestFailed(QStringLiteral("malformed_response"));
      return;
    }
    emit tokenReceived(*result);
  });
}

} // namespace pcm::tokenclient
