#pragma once

#include "fake_schedule_backend.h"
#include "schedule_snapshot.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>

#include <string>

// Behavioural model of the token-backend series routes used by the UI tests:
// capabilities, PUT/GET snapshot with CAS, permanent invitation with a replayable
// Idempotency-Key and the occurrence specialist token. Test double only.
struct SeriesBackendModel {
  struct Series {
    qint64 revision = 0;
    std::string hash;
    qint64 invitationGeneration = 0;
    QString code;
    QString passcode;
  };
  QHash<QString, Series> series;
  QHash<QString, QByteArray> replays; // idempotency key -> response body
  QHash<QString, QString> replayScope;  // idempotency key -> "uid|reissue"
  QStringList requestLog;             // "METHOD path"
  QList<QByteArray> putBodies;
  QStringList invitationKeys;         // Idempotency-Key of every invitation POST
  QStringList specialistPaths;
  bool capabilities = true;
  bool neverRespondToPut = false;
  bool dropNextInvitationReply = false; // create, then close without answering
  int invitations = 0;                  // number of invitations really created
  int invitationStatusOverride = 0;
  int putStatusOverride = 0;          // answer every PUT with this status
  QStringList invalidatedMeetings;    // refs of POST /v1/meetings/{ref}/invalidate

  using R = FakeScheduleBackend::Response;

  R handle(const FakeScheduleBackend::Request &request) {
    requestLog.append(request.method + " " + request.path);
    if (request.path == "/v1/capabilities") {
      return capabilities ? R{.status = 200, .body = R"({"scheduleSeries":true})"}
                          : R{.status = 404, .body = R"({"error":"not_found"})"};
    }
    if (request.path == "/v1/meetings" && request.method == "POST") {
      return {.status = 200,
              .body = R"({"meetingRef":"ref-new","invitationUrl":"https://x/c","passcode":"1"})"};
    }
    if (request.path.startsWith("/v1/meetings/") && request.path.endsWith("/invalidate")) {
      invalidatedMeetings.append(request.path.section('/', 3, 3));
      return {.status = 204, .body = ""};
    }
    const auto uid = request.path.section('/', 3, 3);
    if (request.path.endsWith("/invitation")) {
      return invitation(uid, request);
    }
    if (request.path.endsWith("/specialist-token")) {
      specialistPaths.append(request.path);
      return {.status = 200,
              .body = R"({"endpointUrl":"wss://lk.test","roomName":"room","token":"jwt","expiresAt":9})"};
    }
    if (request.method == "GET") {
      if (!series.contains(uid)) {
        return {.status = 404, .body = R"({"error":"not_found"})"};
      }
      return {.status = 200, .body = QByteArray("{}")};
    }
    putBodies.append(request.body);
    if (putStatusOverride != 0) {
      return {.status = putStatusOverride, .body = R"({"error":"invalid_schedule","reason":"unsupported RRULE field"})"};
    }
    if (neverRespondToPut) {
      return {.neverRespond = true};
    }
    const auto snapshot = pcm::meeting::parseSnapshot(request.body);
    if (!snapshot) {
      return {.status = 422, .body = R"({"error":"invalid_schedule"})"};
    }
    const auto hash = pcm::meeting::scheduleContentHash(*snapshot);
    auto &stored = series[uid];
    if (stored.revision == snapshot->revision && stored.revision != 0) {
      if (stored.hash != hash) {
        return {.status = 409, .body = R"({"error":"revision_conflict"})"};
      }
    } else if (stored.revision != snapshot->baseRevision ||
               snapshot->revision != snapshot->baseRevision + 1) {
      return {.status = 409, .body = R"({"error":"revision_conflict"})"};
    } else {
      stored.revision = snapshot->revision;
      stored.hash = hash;
    }
    return {.status = 200,
            .body = QByteArray(QJsonDocument(QJsonObject{{"series_uid", uid},
                                                         {"revision", static_cast<qint64>(stored.revision)},
                                                         {"content_hash", QString::fromStdString(stored.hash)}})
                                   .toJson(QJsonDocument::Compact))};
  }

private:
  R invitation(const QString &uid, const FakeScheduleBackend::Request &request) {
    const auto key = request.headers.value("idempotency-key");
    invitationKeys.append(key);
    if (invitationStatusOverride != 0) {
      return {.status = invitationStatusOverride,
              .body = R"({"error":"unauthorized"})"};
    }
    const bool reissue = QJsonDocument::fromJson(request.body).object().value("reissue").toBool();
    const auto scope = uid + "|" + (reissue ? "1" : "0");
    if (replays.contains(key)) {
      if (replayScope.value(key) != scope) {
        return {.status = 409, .body = R"({"error":"idempotency_conflict"})"};
      }
      return {.status = 200, .body = replays.value(key)};
    }
    if (!series.contains(uid)) {
      return {.status = 404, .body = R"({"error":"not_found"})"};
    }
    auto &stored = series[uid];
    if (stored.invitationGeneration > 0 && !reissue) {
      return {.status = 409, .body = R"({"error":"invitation_exists"})"};
    }
    ++stored.invitationGeneration;
    ++invitations;
    stored.code = QStringLiteral("code-%1-%2").arg(uid.left(4)).arg(stored.invitationGeneration);
    stored.passcode = QStringLiteral("pass%1").arg(stored.invitationGeneration, 4, 10, QLatin1Char('0'));
    const QByteArray body = QJsonDocument(QJsonObject{
        {"invitation_url", QStringLiteral("https://sessio.test/i/") + stored.code},
        {"passcode", stored.passcode},
        {"generation", static_cast<qint64>(stored.invitationGeneration)}})
                                .toJson(QJsonDocument::Compact);
    replays.insert(key, body);
    replayScope.insert(key, scope);
    if (dropNextInvitationReply) {
      dropNextInvitationReply = false;
      return {.dropConnection = true};
    }
    return {.status = 200, .body = body};
  }
};
