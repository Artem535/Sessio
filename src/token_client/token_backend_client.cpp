#include "token_backend_client.h"
#include "token_response_parser.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimeZone>

#include <algorithm>

namespace pcm::tokenclient {

TokenBackendClient::TokenBackendClient(QString baseUrl, QObject *parent)
    : QObject(parent), mBaseUrl(std::move(baseUrl)) {
  qRegisterMetaType<TokenResult>();
  qRegisterMetaType<MeetingCreateResult>();
  qRegisterMetaType<ScheduleHttpResult>();
}

QString formatOriginalStartUtc(const qint64 originalStartMs) {
  const qint64 seconds = originalStartMs >= 0 ? originalStartMs / 1000
                                              : -((-originalStartMs + 999) / 1000);
  return QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::UTC)
      .toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'"));
}

void TokenBackendClient::setBaseUrl(const QString &baseUrl) { mBaseUrl = baseUrl; }

void TokenBackendClient::requestSpecialistToken(const QString &bearerCredential,
                                                const QString &meetingRef, const QString &displayName) {
  const auto body = displayName.trimmed().isEmpty() ? QByteArray() :
      QJsonDocument(QJsonObject{{"displayName", displayName.trimmed()}}).toJson(QJsonDocument::Compact);
  post(QStringLiteral("/v1/meetings/%1/specialist-token").arg(meetingRef), body,
       bearerCredential);
}

void TokenBackendClient::requestOccurrenceSpecialistToken(const QString &bearerCredential,
                                                          const QString &seriesUid,
                                                          const qint64 originalStartMs,
                                                          const QString &displayName) {
  const auto body = displayName.trimmed().isEmpty() ? QByteArray() :
      QJsonDocument(QJsonObject{{"displayName", displayName.trimmed()}}).toJson(QJsonDocument::Compact);
  post(QStringLiteral("/v1/schedule-series/%1/occurrences/%2/specialist-token")
           .arg(seriesUid, formatOriginalStartUtc(originalStartMs)),
       body, bearerCredential);
}

void TokenBackendClient::requestClientToken(const QString &invitationCode,
                                            const QString &passcode, const QString &displayName) {
  QJsonObject bodyObject;
  bodyObject["passcode"] = passcode;
  if (!displayName.trimmed().isEmpty()) bodyObject["displayName"] = displayName.trimmed();
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

template <class Done>
void TokenBackendClient::sendSchedule(const QByteArray &verb, const QString &path,
                                      const QByteArray &body, const QString &bearerCredential,
                                      const QList<QPair<QByteArray, QByteArray>> &headers,
                                      const QString &seriesUid, const qint64 revision, Done done) {
  QNetworkRequest request(QUrl(mBaseUrl + path));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setTransferTimeout(mScheduleTimeoutMs);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  if (!bearerCredential.isEmpty()) {
    request.setRawHeader("Authorization", "Bearer " + bearerCredential.toUtf8());
  }
  for (const auto &header : headers) {
    request.setRawHeader(header.first, header.second);
  }

  QNetworkReply *reply = nullptr;
  if (verb == "GET") {
    reply = mNetworkManager.get(request);
  } else {
    reply = mNetworkManager.sendCustomRequest(request, verb, body);
  }
  connect(reply, &QNetworkReply::finished, this,
          [reply, seriesUid, revision, done = std::move(done)]() {
            reply->deleteLater();
            ScheduleHttpResult result;
            result.seriesUid = seriesUid;
            result.revision = revision;
            result.httpStatus =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            result.timedOut = reply->error() == QNetworkReply::TimeoutError ||
                             reply->error() == QNetworkReply::OperationCanceledError;
            result.body = reply->readAll();
            if (!result.ok()) {
              const auto parsed = QJsonDocument::fromJson(result.body).object();
              result.errorCode = parsed.value("error").toString();
              result.reason = parsed.value("reason").toString();
            }
            bool parsedRetry = false;
            const auto retry = reply->rawHeader("Retry-After").toInt(&parsedRetry);
            if (parsedRetry) {
              result.retryAfterSeconds = std::clamp(retry, 0, 3600);
            }
            done(result);
          });
}

void TokenBackendClient::requestScheduleCapabilities(const QString &bearerCredential) {
  sendSchedule("GET", QStringLiteral("/v1/capabilities"), {}, bearerCredential, {}, {}, 0,
               [this](const ScheduleHttpResult &result) {
                 emit scheduleCapabilitiesFinished(result);
               });
}

void TokenBackendClient::putSchedule(const QString &bearerCredential, const QString &seriesUid,
                                     const qint64 revision, const QByteArray &body) {
  sendSchedule("PUT", QStringLiteral("/v1/schedule-series/%1").arg(seriesUid), body,
               bearerCredential, {}, seriesUid, revision,
               [this](const ScheduleHttpResult &result) { emit schedulePutFinished(result); });
}

void TokenBackendClient::getSchedule(const QString &bearerCredential, const QString &seriesUid) {
  sendSchedule("GET", QStringLiteral("/v1/schedule-series/%1").arg(seriesUid), {},
               bearerCredential, {}, seriesUid, 0,
               [this](const ScheduleHttpResult &result) { emit scheduleGetFinished(result); });
}

void TokenBackendClient::requestSeriesInvitation(const QString &bearerCredential,
                                                 const QString &seriesUid,
                                                 const QString &idempotencyKey,
                                                 const bool reissue) {
  const auto body =
      QJsonDocument(QJsonObject{{"reissue", reissue}}).toJson(QJsonDocument::Compact);
  sendSchedule("POST", QStringLiteral("/v1/schedule-series/%1/invitation").arg(seriesUid), body,
               bearerCredential, {{"Idempotency-Key", idempotencyKey.toUtf8()}}, seriesUid, 0,
               [this](const ScheduleHttpResult &result) { emit seriesInvitationFinished(result); });
}

void TokenBackendClient::requestSeriesRevoke(const QString &bearerCredential,
                                             const QString &seriesUid) {
  sendSchedule("POST", QStringLiteral("/v1/schedule-series/%1/revoke").arg(seriesUid), "{}",
               bearerCredential, {}, seriesUid, 0,
               [this](const ScheduleHttpResult &result) { emit seriesRevokeFinished(result); });
}

} // namespace pcm::tokenclient
