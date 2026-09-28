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
  qRegisterMetaType<MeetingCreateResult>();
}

void TokenBackendClient::setBaseUrl(const QString &baseUrl) { mBaseUrl = baseUrl; }

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

QNetworkReply *TokenBackendClient::sendPost(const QString &path, const QByteArray &body,
                                            const QString &bearerCredential) {
  QNetworkRequest request(QUrl(mBaseUrl + path));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  if (!bearerCredential.isEmpty()) {
    request.setRawHeader("Authorization", "Bearer " + bearerCredential.toUtf8());
  }
  return mNetworkManager.post(request, body);
}

void TokenBackendClient::post(const QString &path, const QByteArray &body,
                              const QString &bearerCredential) {
  auto *reply = sendPost(path, body, bearerCredential);
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

void TokenBackendClient::requestCreateMeeting(const QString &bearerCredential,
                                              const QString &scheduledStartIso,
                                              const QString &scheduledEndIso) {
  QJsonObject bodyObject;
  bodyObject["scheduledStart"] = scheduledStartIso;
  bodyObject["scheduledEnd"] = scheduledEndIso;
  const QByteArray body = QJsonDocument(bodyObject).toJson(QJsonDocument::Compact);

  auto *reply = sendPost(QStringLiteral("/v1/meetings"), body, bearerCredential);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();
    const auto responseBody = reply->readAll();
    const auto httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError || httpStatus != 200) {
      emit meetingCreateFailed(parseErrorMessage(responseBody));
      return;
    }

    const auto result = parseMeetingCreateResponse(responseBody);
    if (!result.has_value()) {
      emit meetingCreateFailed(QStringLiteral("malformed_response"));
      return;
    }
    emit meetingCreated(*result);
  });
}

void TokenBackendClient::requestInvalidateMeeting(const QString &bearerCredential,
                                                  const QString &meetingRef) {
  auto *reply = sendPost(QStringLiteral("/v1/meetings/%1/invalidate").arg(meetingRef), QByteArray(),
                        bearerCredential);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();
    const auto responseBody = reply->readAll();
    const auto httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // The token-backend answers a successful invalidate with 204 No Content
    // (no body to parse); any 2xx counts as success here.
    if (reply->error() != QNetworkReply::NoError || httpStatus < 200 || httpStatus >= 300) {
      emit meetingInvalidateFailed(parseErrorMessage(responseBody));
      return;
    }
    emit meetingInvalidated();
  });
}

} // namespace pcm::tokenclient
