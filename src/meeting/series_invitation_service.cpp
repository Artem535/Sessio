#include "series_invitation_service.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QTimer>

#include <algorithm>

namespace pcm::meeting {
namespace {

QString scheduleWaitDetail(const ScheduleSyncState state) {
  switch (state) {
  case ScheduleSyncState::Conflict:
    return QStringLiteral("schedule_conflict");
  case ScheduleSyncState::Rejected:
    return QStringLiteral("schedule_rejected");
  case ScheduleSyncState::Unsupported:
    return QStringLiteral("schedule_unsupported");
  case ScheduleSyncState::Unauthorized:
    return QStringLiteral("schedule_unauthorized");
  case ScheduleSyncState::CredentialUnavailable:
    return QStringLiteral("schedule_credential_unavailable");
  default:
    return QStringLiteral("schedule_not_acknowledged");
  }
}

} // namespace

SeriesInvitationService::SeriesInvitationService(pcm::database::Database &db, ScheduleSync &sync,
                                                 pcm::tokenclient::TokenBackendClient &client,
                                                 ScheduleSync::CredentialReader credentialReader,
                                                 SeriesInvitationSecretStore &store,
                                                 QObject *parent)
    : QObject(parent), mDb(db), mSync(sync), mClient(client),
      mReadCredential(std::move(credentialReader)), mStore(store) {
  qRegisterMetaType<InvitationStatus>();
  mBackoff = [](int attempt) {
    return ScheduleSync::retryDelayMs(attempt, QRandomGenerator::global()->generateDouble());
  };
  connect(&mClient, &pcm::tokenclient::TokenBackendClient::seriesInvitationFinished, this,
          &SeriesInvitationService::onInvitationFinished);
  connect(&mSync, &ScheduleSync::statusChanged, this,
          [this](const QString &uid, const ScheduleSyncStatus &) { onScheduleStatus(uid); });
}

SeriesInvitationService::~SeriesInvitationService() = default;

void SeriesInvitationService::ensureInvitation(const int64_t seriesId) { request(seriesId, false); }

void SeriesInvitationService::reissueInvitation(const int64_t seriesId) {
  if (mRuntime.value(seriesId).inflight) {
    return;
  }
  // A new intent: drop the persisted key of any earlier attempt so the server
  // does not treat this as a replay of it.
  mDb.clear_schedule_invitation_key(seriesId);
  request(seriesId, true);
}

void SeriesInvitationService::resumeInterruptedReissue(const int64_t seriesId) {
  const auto identity = mDb.get_schedule_identity(seriesId);
  if (!identity.has_value() || identity->invitation_generation <= 0 ||
      !identity->invitation_key.has_value() || identity->invitation_key->empty() ||
      mRuntime.value(seriesId).inflight) {
    return;
  }
  request(seriesId, true); // keeps the persisted key: ensure_schedule_invitation_key reuses it
}

void SeriesInvitationService::retry(const int64_t seriesId) {
  request(seriesId, mRuntime.value(seriesId).reissue);
}

InvitationStatus SeriesInvitationService::status(const int64_t seriesId) {
  InvitationStatus result;
  if (const auto identity = mDb.get_schedule_identity(seriesId)) {
    result.generation = identity->invitation_generation;
    result.state = identity->invitation_generation > 0 ? InvitationState::Ready
                                                       : InvitationState::None;
  }
  if (const auto it = mRuntime.constFind(seriesId);
      it != mRuntime.constEnd() && it->state != InvitationState::None) {
    result.state = it->state;
    result.detail = it->detail;
  }
  return result;
}

void SeriesInvitationService::loadInvitation(const int64_t seriesId, const LoadCallback &done) {
  const auto identity = mDb.get_schedule_identity(seriesId);
  if (!identity.has_value()) {
    done(true, {}, {});
    return;
  }
  mStore.read(QString::fromStdString(identity->series_uid),
              [done](const bool ok, const std::optional<SeriesInvitationSecret> &secret) {
                if (!ok) {
                  done(false, {}, {});
                } else if (!secret.has_value()) {
                  done(true, {}, {});
                } else {
                  done(true, secret->url, secret->passcode);
                }
              });
}

void SeriesInvitationService::request(const int64_t seriesId, const bool reissue) {
  const auto identity = mDb.get_schedule_identity(seriesId);
  auto &runtime = mRuntime[seriesId];
  if (!identity.has_value()) {
    setState(seriesId, InvitationState::Failed, QStringLiteral("no_schedule_identity"));
    return;
  }
  runtime.uid = QString::fromStdString(identity->series_uid);
  if (!reissue && identity->invitation_generation > 0) {
    // The permanent invitation exists; nothing (a title edit, a schedule edit)
    // may replace it. Only reissueInvitation() does.
    runtime.wanted = false;
    setState(seriesId, InvitationState::Ready);
    return;
  }
  runtime.wanted = true;
  runtime.reissue = reissue;
  if (runtime.inflight) {
    return;
  }
  if (runtime.timer) {
    runtime.timer->stop();
  }

  const auto sync = mSync.status(runtime.uid);
  if (sync.state != ScheduleSyncState::Synced || sync.ackedRevision <= 0) {
    // No invitation for a schedule the server has not acknowledged (yet).
    setState(seriesId, InvitationState::WaitingForSchedule, scheduleWaitDetail(sync.state));
    return;
  }
  send(seriesId);
}

void SeriesInvitationService::onScheduleStatus(const QString &uid) {
  for (auto it = mRuntime.begin(); it != mRuntime.end(); ++it) {
    if (it->uid == uid && it->wanted && !it->inflight &&
        it->state == InvitationState::WaitingForSchedule) {
      const auto seriesId = it.key();
      request(seriesId, it->reissue);
      return;
    }
  }
}

void SeriesInvitationService::send(const int64_t seriesId) {
  auto &runtime = mRuntime[seriesId];
  const auto key = mDb.ensure_schedule_invitation_key(seriesId);
  if (key.empty()) {
    setState(seriesId, InvitationState::Failed, QStringLiteral("local_state_unavailable"));
    return;
  }
  runtime.inflight = true;
  setState(seriesId, InvitationState::Requesting);

  QPointer<SeriesInvitationService> guard(this);
  const auto uid = runtime.uid;
  const bool reissue = runtime.reissue;
  const auto idempotencyKey = QString::fromStdString(key);
  mReadCredential([guard, seriesId, uid, reissue, idempotencyKey](const bool ok,
                                                                  const QString &credential) {
    if (!guard) {
      return;
    }
    const auto it = guard->mRuntime.find(seriesId);
    if (it == guard->mRuntime.end() || !it->inflight) {
      return;
    }
    if (!ok || credential.isEmpty()) {
      it->inflight = false;
      guard->setState(seriesId, InvitationState::Failed, QStringLiteral("credential_unavailable"));
      return;
    }
    guard->mClient.requestSeriesInvitation(credential, uid, idempotencyKey, reissue);
  });
}

void SeriesInvitationService::onInvitationFinished(
    const pcm::tokenclient::ScheduleHttpResult &result) {
  int64_t seriesId = 0;
  for (auto it = mRuntime.begin(); it != mRuntime.end(); ++it) {
    if (it->inflight && it->uid == result.seriesUid) {
      seriesId = it.key();
      break;
    }
  }
  if (seriesId == 0) {
    return; // not a request this service owns
  }
  auto &runtime = mRuntime[seriesId];

  if (result.ok()) {
    const auto reply = QJsonDocument::fromJson(result.body).object();
    SeriesInvitationSecret secret;
    secret.url = reply.value("invitation_url").toString();
    secret.passcode = reply.value("passcode").toString();
    secret.generation = static_cast<qint64>(reply.value("generation").toDouble(0));
    if (secret.url.isEmpty() || secret.passcode.isEmpty() || secret.generation <= 0) {
      runtime.inflight = false;
      setState(seriesId, InvitationState::Failed, QStringLiteral("malformed_response"));
      return;
    }
    store(seriesId, secret);
    return;
  }

  runtime.inflight = false;
  if (result.httpStatus == 409 && result.errorCode == QLatin1String("invitation_exists")) {
    // The server already holds an invitation this device has no secret for.
    runtime.wanted = false;
    setState(seriesId, InvitationState::NeedsReissue, result.errorCode);
  } else if (result.httpStatus == 410 ||
             (result.httpStatus == 409 && result.errorCode == QLatin1String("idempotency_conflict"))) {
    // The replay window ended or the key belongs to another intent: this
    // attempt cannot be resumed, only a deliberate new request can proceed.
    mDb.clear_schedule_invitation_key(seriesId);
    runtime.wanted = false;
    setState(seriesId, InvitationState::NeedsReissue,
             result.errorCode.isEmpty() ? QStringLiteral("invitation_replay_expired")
                                        : result.errorCode);
  } else if (result.httpStatus == 401 || result.httpStatus == 403) {
    setState(seriesId, InvitationState::Failed, QStringLiteral("unauthorized"));
  } else if (result.httpStatus == 429) {
    setState(seriesId, InvitationState::WaitingForNetwork, QStringLiteral("too_many_attempts"));
    scheduleRetry(seriesId, result.retryAfterSeconds * 1000);
  } else if (result.httpStatus == 0 || result.timedOut || result.httpStatus >= 500) {
    setState(seriesId, InvitationState::WaitingForNetwork,
             result.timedOut ? QStringLiteral("timeout") : QStringLiteral("unreachable"));
    scheduleRetry(seriesId, 0);
  } else {
    setState(seriesId, InvitationState::Failed,
             !result.errorCode.isEmpty() ? result.errorCode
                                         : QStringLiteral("http_%1").arg(result.httpStatus));
  }
}

void SeriesInvitationService::store(const int64_t seriesId, const SeriesInvitationSecret &secret) {
  auto &runtime = mRuntime[seriesId];
  setState(seriesId, InvitationState::StoringSecret);
  QPointer<SeriesInvitationService> guard(this);
  const auto uid = runtime.uid;
  mStore.write(uid, secret, [guard, seriesId, generation = secret.generation](const bool ok) {
    if (!guard) {
      return;
    }
    const auto it = guard->mRuntime.find(seriesId);
    if (it == guard->mRuntime.end()) {
      return;
    }
    it->inflight = false;
    if (!ok) {
      // Keep the persisted key: retrying within the replay window returns the
      // very same secret instead of creating another invitation.
      guard->setState(seriesId, InvitationState::Failed, QStringLiteral("secret_store_unavailable"));
      return;
    }
    if (!guard->mDb.set_schedule_invitation_generation(seriesId, generation)) {
      guard->setState(seriesId, InvitationState::Failed, QStringLiteral("local_state_unavailable"));
      return;
    }
    guard->mDb.clear_schedule_invitation_key(seriesId);
    it->wanted = false;
    it->reissue = false;
    it->attempts = 0;
    guard->setState(seriesId, InvitationState::Ready);
    emit guard->invitationReady(seriesId);
  });
}

void SeriesInvitationService::scheduleRetry(const int64_t seriesId, const int minDelayMs) {
  auto &runtime = mRuntime[seriesId];
  if (!runtime.timer) {
    runtime.timer = new QTimer(this);
    runtime.timer->setSingleShot(true);
    connect(runtime.timer, &QTimer::timeout, this, [this, seriesId] {
      const auto it = mRuntime.find(seriesId);
      if (it != mRuntime.end() && it->wanted && !it->inflight) {
        request(seriesId, it->reissue);
      }
    });
  }
  runtime.timer->start(std::max(mBackoff(runtime.attempts++), minDelayMs));
}

void SeriesInvitationService::setState(const int64_t seriesId, const InvitationState state,
                                       const QString &detail) {
  auto &runtime = mRuntime[seriesId];
  runtime.state = state;
  runtime.detail = detail;
  emit statusChanged(seriesId, status(seriesId));
}

} // namespace pcm::meeting
