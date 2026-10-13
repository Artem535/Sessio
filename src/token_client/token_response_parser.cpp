#include "token_response_parser.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace pcm::tokenclient {

std::optional<TokenResult> parseTokenResponse(const QByteArray &json) {
  const auto doc = QJsonDocument::fromJson(json);
  if (!doc.isObject()) {
    return std::nullopt;
  }
  const auto obj = doc.object();
  if (!obj.contains("endpointUrl") || !obj.contains("roomName") ||
      !obj.contains("token") || !obj.contains("expiresAt")) {
    return std::nullopt;
  }

  TokenResult result;
  result.endpointUrl = obj.value("endpointUrl").toString();
  result.roomName = obj.value("roomName").toString();
  result.token = obj.value("token").toString();
  result.expiresAt = static_cast<qint64>(obj.value("expiresAt").toDouble());
  return result;
}

std::optional<MeetingCreateResult> parseMeetingCreateResponse(const QByteArray &json) {
  const auto doc = QJsonDocument::fromJson(json);
  if (!doc.isObject()) {
    return std::nullopt;
  }
  const auto obj = doc.object();
  if (!obj.contains("meetingRef") || !obj.contains("invitationUrl") || !obj.contains("passcode")) {
    return std::nullopt;
  }
  MeetingCreateResult result;
  result.meetingRef = obj.value("meetingRef").toString();
  result.invitationUrl = obj.value("invitationUrl").toString();
  result.passcode = obj.value("passcode").toString();
  return result;
}

QString parseErrorMessage(const QByteArray &json) {
  const auto doc = QJsonDocument::fromJson(json);
  if (!doc.isObject() || !doc.object().contains("error")) {
    return QStringLiteral("request_failed");
  }
  return doc.object().value("error").toString();
}

} // namespace pcm::tokenclient
