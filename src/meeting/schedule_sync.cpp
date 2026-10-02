#include "schedule_sync.h"

#include "schedule_snapshot.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QTimer>

#include <algorithm>

namespace pcm::meeting {
namespace {

using pcm::database::schedule_sync_state::kConflict;
using pcm::database::schedule_sync_state::kPending;
using pcm::database::schedule_sync_state::kRejected;

constexpr int kMaxBackoffMs = 300000;

std::optional<qint64> integerField(const QJsonObject &object, const char *key) {
  const auto value = object.value(QLatin1String(key));
  if (!value.isDouble()) {
    return std::nullopt;
  }
  return static_cast<qint64>(value.toDouble());
}

} // namespace

ScheduleSync::ScheduleSync(pcm::database::Database &db, pcm::tokenclient::TokenBackendClient &client,
                           CredentialReader credentialReader, QObject *parent)
    : QObject(parent), mDb(db), mClient(client), mReadCredential(std::move(credentialReader)) {
  qRegisterMetaType<ScheduleSyncStatus>();
  qRegisterMetaType<ScheduleCapability>();
  mBackoff = [](int attempt) {
    return retryDelayMs(attempt, QRandomGenerator::global()->generateDouble());
  };
  mProbeTimer = new QTimer(this);
  mProbeTimer->setSingleShot(true);
  connect(mProbeTimer, &QTimer::timeout, this, [this] {
    if (mCapability == ScheduleCapability::Unreachable ||
        (mCapability == ScheduleCapability::Unknown && mCredentialBlocked)) {
      mCapability = ScheduleCapability::Unknown;
      pump();
    }
  });
  connect(&mClient, &pcm::tokenclient::TokenBackendClient::scheduleCapabilitiesFinished, this,
          &ScheduleSync::onCapabilities);
  connect(&mClient, &pcm::tokenclient::TokenBackendClient::schedulePutFinished, this,
          &ScheduleSync::onPutFinished);
  connect(&mClient, &pcm::tokenclient::TokenBackendClient::scheduleGetFinished, this,
          &ScheduleSync::onGetFinished);
}

ScheduleSync::~ScheduleSync() = default;

int ScheduleSync::retryDelayMs(const int attempt, const double jitterUnit) {
  static constexpr int kBase[] = {2000, 5000, 15000, 60000};
  const int base = attempt < 0 ? kBase[0]
                               : attempt < 4 ? kBase[attempt] : kMaxBackoffMs;
  const double unit = std::clamp(jitterUnit, 0.0, 1.0);
  const auto jittered = static_cast<int>(base * (0.8 + 0.4 * unit));
  return std::min(jittered, kMaxBackoffMs);
}

int ScheduleSync::nextDelayMs(const int attempt) const { return mBackoff(attempt); }

void ScheduleSync::start() {
  mStarted = true;
  emitAllStatuses();
  pump();
}

void ScheduleSync::notifyLocalChange(const QString &seriesUid) {
  // A new edit supersedes a content rejection (persisted state already moved
  // back to pending by the commit) and cancels a stale backoff wait.
  if (auto it = mRuntime.find(seriesUid); it != mRuntime.end()) {
    if (it->transient == ScheduleSyncState::WaitingForNetwork && it->timer) {
      it->timer->stop();
      it->transient.reset();
    }
    it->attempts = 0;
  }
  emitStatus(seriesUid);
  if (mStarted) {
    pump();
  }
}

void ScheduleSync::wake() {
  for (auto it = mRuntime.begin(); it != mRuntime.end(); ++it) {
    if (it->timer) {
      it->timer->stop();
    }
    if (it->transient != ScheduleSyncState::Sending) {
      it->transient.reset();
    }
    it->attempts = 0;
  }
  mProbeTimer->stop();
  mProbeAttempts = 0;
  mCredentialBlocked = false;
  if (mCapability != ScheduleCapability::Checking && mCapability != ScheduleCapability::Supported) {
    mCapability = ScheduleCapability::Unknown;
  }
  emitAllStatuses();
  pump();
}

void ScheduleSync::retry(const QString &seriesUid) {
  const auto identity = mDb.get_schedule_identity_by_uid(seriesUid.toStdString());
  if (!identity.has_value()) {
    return;
  }
  if (identity->sync_state == kRejected) {
    mDb.set_schedule_sync_state(seriesUid.toStdString(), kPending);
  }
  if (auto it = mRuntime.find(seriesUid); it != mRuntime.end()) {
    if (it->timer) {
      it->timer->stop();
    }
    if (it->transient != ScheduleSyncState::Sending) {
      it->transient.reset();
    }
    it->attempts = 0;
  }
  if (mCapability == ScheduleCapability::Unauthorized ||
      mCapability == ScheduleCapability::Unreachable) {
    mCapability = ScheduleCapability::Unknown;
  }
  emitStatus(seriesUid);
  pump();
}

ScheduleSyncStatus ScheduleSync::status(const QString &seriesUid) {
  ScheduleSyncStatus result;
  const auto uid = seriesUid.toStdString();
  const auto identity = mDb.get_schedule_identity_by_uid(uid);
  if (!identity.has_value()) {
    return result;
  }
  const auto outbox = mDb.get_schedule_outbox(uid);
  const bool queued =
      outbox.has_value() && (outbox->pending_payload.has_value() || outbox->inflight_payload.has_value());
  result.ackedRevision = identity->acked_revision;
  result.desiredRevision = identity->desired_revision;
  result.detail = QString::fromStdString(identity->last_error);
  result.unconfirmed = queued || identity->sync_state != pcm::database::schedule_sync_state::kSynced;

  const auto runtime = mRuntime.constFind(seriesUid);
  if (identity->sync_state == kConflict) {
    result.state = ScheduleSyncState::Conflict;
  } else if (identity->sync_state == kRejected) {
    result.state = ScheduleSyncState::Rejected;
  } else if (!queued) {
    result.state = ScheduleSyncState::Synced;
  } else if (mCapability == ScheduleCapability::Unsupported) {
    result.state = ScheduleSyncState::Unsupported;
  } else if (mCapability == ScheduleCapability::Unauthorized) {
    result.state = ScheduleSyncState::Unauthorized;
  } else if (runtime != mRuntime.constEnd() && runtime->transient.has_value()) {
    result.state = *runtime->transient;
    result.detail = runtime->detail;
  } else if (mCapability == ScheduleCapability::Unreachable) {
    result.state = ScheduleSyncState::WaitingForNetwork;
  } else if (mCapability == ScheduleCapability::Unknown && mCredentialBlocked) {
    result.state = ScheduleSyncState::CredentialUnavailable;
  } else {
    result.state = ScheduleSyncState::Queued;
  }
  return result;
}

void ScheduleSync::pump() {
  if (mActive.has_value() || !mStarted) {
    return;
  }
  if (mCapability == ScheduleCapability::Unknown) {
    probeCapability();
    return;
  }
  if (mCapability != ScheduleCapability::Supported) {
    return;
  }
  for (const auto &identity : mDb.list_schedule_series_pending_sync()) {
    if (identity.sync_state == kConflict || identity.sync_state == kRejected) {
      continue;
    }
    const auto uid = QString::fromStdString(identity.series_uid);
    const auto runtime = mRuntime.constFind(uid);
    if (runtime != mRuntime.constEnd()) {
      if (runtime->timer && runtime->timer->isActive()) {
        continue;
      }
      if (runtime->transient == ScheduleSyncState::Unauthorized) {
        continue; // waits for wake()/retry()
      }
    }
    send(identity);
    return;
  }
}

void ScheduleSync::probeCapability() {
  mCapability = ScheduleCapability::Checking;
  mCredentialBlocked = false;
  QPointer<ScheduleSync> guard(this);
  mReadCredential([guard](bool ok, const QString &credential) {
    if (!guard || guard->mCapability != ScheduleCapability::Checking) {
      return;
    }
    if (!ok || credential.isEmpty()) {
      guard->mCapability = ScheduleCapability::Unknown;
      guard->mCredentialBlocked = true;
      guard->scheduleProbeRetry();
      guard->emitAllStatuses();
      return;
    }
    guard->mClient.requestScheduleCapabilities(credential);
  });
}

void ScheduleSync::onCapabilities(const pcm::tokenclient::ScheduleHttpResult &result) {
  if (mCapability != ScheduleCapability::Checking) {
    return;
  }
  if (result.ok()) {
    const auto flag = QJsonDocument::fromJson(result.body).object().value("scheduleSeries");
    mCapability = flag.toBool(false) ? ScheduleCapability::Supported
                                     : ScheduleCapability::Unsupported;
    mProbeAttempts = 0;
  } else if (result.httpStatus == 404) {
    mCapability = ScheduleCapability::Unsupported;
  } else if (result.httpStatus == 401 || result.httpStatus == 403) {
    mCapability = ScheduleCapability::Unauthorized;
  } else {
    mCapability = ScheduleCapability::Unreachable;
    scheduleProbeRetry();
  }
  emit capabilityChanged(mCapability);
  emitAllStatuses();
  pump();
}

void ScheduleSync::scheduleProbeRetry() {
  mProbeTimer->start(nextDelayMs(mProbeAttempts++));
}

void ScheduleSync::send(const pcm::database::ScheduleIdentity &identity) {
  const auto uid = QString::fromStdString(identity.series_uid);
  const auto outbox = mDb.get_schedule_outbox(identity.series_uid);
  if (!outbox.has_value()) {
    return;
  }

  std::string wire;
  std::string hash;
  qint64 revision = 0;
  if (outbox->inflight_payload.has_value() && outbox->inflight_revision.has_value()) {
    // Resend the frozen bytes: same revision, same content, same hash.
    wire = *outbox->inflight_payload;
    hash = outbox->inflight_hash.value_or("");
    revision = *outbox->inflight_revision;
  } else if (outbox->pending_payload.has_value() && outbox->pending_desired_revision.has_value()) {
    auto snapshot = parseSnapshot(QByteArray::fromStdString(*outbox->pending_payload));
    if (!snapshot.has_value()) {
      mDb.set_schedule_sync_state(identity.series_uid, kRejected, "invalid_queued_payload");
      setTransient(uid, std::nullopt, QStringLiteral("invalid_queued_payload"));
      return;
    }
    revision = identity.acked_revision + 1;
    snapshot->revision = revision;
    snapshot->baseRevision = identity.acked_revision;
    wire = serializeSnapshot(*snapshot).toStdString();
    hash = scheduleContentHash(*snapshot);
    if (!mDb.freeze_schedule_pending(identity.series_uid, *outbox->pending_desired_revision,
                                     revision, wire, hash)) {
      QTimer::singleShot(0, this, [this] { pump(); });
      return;
    }
  } else {
    return;
  }

  Active active;
  active.uid = uid;
  active.revision = revision;
  active.token = ++mNextToken;
  active.hash = hash;
  mActive = active;
  setTransient(uid, ScheduleSyncState::Sending);

  QPointer<ScheduleSync> guard(this);
  const auto token = active.token;
  const auto body = QByteArray::fromStdString(wire);
  mReadCredential([guard, token, uid, revision, body](bool ok, const QString &credential) {
    if (!guard || !guard->mActive.has_value() || guard->mActive->token != token) {
      return;
    }
    if (!ok || credential.isEmpty()) {
      guard->mActive.reset();
      guard->setTransient(uid, ScheduleSyncState::CredentialUnavailable,
                          QStringLiteral("credential_unavailable"));
      guard->scheduleRetry(uid, 0);
      return;
    }
    guard->mClient.putSchedule(credential, uid, revision, body);
  });
}

void ScheduleSync::onPutFinished(const pcm::tokenclient::ScheduleHttpResult &result) {
  if (!mActive.has_value() || result.seriesUid != mActive->uid ||
      result.revision != mActive->revision) {
    return; // answer for a request this service no longer owns
  }
  const auto active = *mActive;
  mActive.reset();
  const auto uid = active.uid;
  const auto uidStd = uid.toStdString();

  if (result.ok()) {
    const auto reply = QJsonDocument::fromJson(result.body).object();
    const auto ackRevision = integerField(reply, "revision");
    const auto ackHash = reply.value("content_hash").toString().toLower().toStdString();
    if (ackRevision != active.revision || ackHash != active.hash) {
      // The server stored different content for our revision: not our write.
      mDb.set_schedule_sync_state(uidStd, kConflict, "server_content_mismatch");
      setTransient(uid, std::nullopt, QStringLiteral("server_content_mismatch"));
    } else if (mDb.ack_schedule_inflight(uidStd, active.revision, active.hash).has_value()) {
      mRuntime[uid].attempts = 0;
      setTransient(uid, std::nullopt);
      emit synced(uid, active.revision);
    } else {
      scheduleRetry(uid, 0); // could not record the ACK; replay is idempotent
    }
  } else if (result.httpStatus == 409) {
    mDb.set_schedule_sync_state(uidStd, kConflict, "revision_conflict");
    setTransient(uid, std::nullopt, QStringLiteral("revision_conflict"));
  } else if (result.httpStatus == 401 || result.httpStatus == 403) {
    setTransient(uid, ScheduleSyncState::Unauthorized, QStringLiteral("unauthorized"));
  } else if (result.httpStatus == 429) {
    setTransient(uid, ScheduleSyncState::WaitingForNetwork, QStringLiteral("too_many_attempts"));
    scheduleRetry(uid, result.retryAfterSeconds * 1000);
  } else if (result.httpStatus == 0 || result.timedOut || result.httpStatus >= 500) {
    setTransient(uid, ScheduleSyncState::WaitingForNetwork,
                 result.timedOut ? QStringLiteral("timeout") : QStringLiteral("unreachable"));
    scheduleRetry(uid, 0);
  } else {
    // 404/413/422/other 4xx: the content (or ownership) is refused. Keep the
    // local data and the frozen payload; stop automatic retries.
    const auto reason = !result.reason.isEmpty() ? result.reason
                        : !result.errorCode.isEmpty() ? result.errorCode
                                                       : QStringLiteral("http_%1").arg(result.httpStatus);
    mDb.set_schedule_sync_state(uidStd, kRejected, reason.toStdString());
    setTransient(uid, std::nullopt, reason);
  }
  emitStatus(uid);
  QTimer::singleShot(0, this, [this] { pump(); });
}

void ScheduleSync::scheduleRetry(const QString &uid, const int minDelayMs) {
  auto &runtime = mRuntime[uid];
  if (!runtime.timer) {
    runtime.timer = new QTimer(this);
    runtime.timer->setSingleShot(true);
    connect(runtime.timer, &QTimer::timeout, this, [this, uid] {
      auto it = mRuntime.find(uid);
      if (it != mRuntime.end() && it->transient != ScheduleSyncState::Sending) {
        it->transient.reset();
      }
      emitStatus(uid);
      pump();
    });
  }
  const auto delay = std::max(nextDelayMs(runtime.attempts++), minDelayMs);
  runtime.timer->start(delay);
}

void ScheduleSync::setTransient(const QString &uid, const std::optional<ScheduleSyncState> state,
                                const QString &detail) {
  auto &runtime = mRuntime[uid];
  runtime.transient = state;
  runtime.detail = detail;
  emitStatus(uid);
}

void ScheduleSync::emitStatus(const QString &uid) { emit statusChanged(uid, status(uid)); }

void ScheduleSync::emitAllStatuses() {
  for (const auto &identity : mDb.list_schedule_series_pending_sync()) {
    emitStatus(QString::fromStdString(identity.series_uid));
  }
}

void ScheduleSync::fetchServerSnapshot(const QString &seriesUid) {
  requestGet(seriesUid, GetPurpose::View);
}

void ScheduleSync::publishRestoredSchedule(const QString &seriesUid) {
  requestGet(seriesUid, GetPurpose::Publish);
}

void ScheduleSync::requestGet(const QString &uid, const GetPurpose purpose) {
  if (mPendingGets.contains(uid)) {
    return;
  }
  mPendingGets.insert(uid, purpose);
  QPointer<ScheduleSync> guard(this);
  mReadCredential([guard, uid](bool ok, const QString &credential) {
    if (!guard || !guard->mPendingGets.contains(uid)) {
      return;
    }
    if (!ok || credential.isEmpty()) {
      guard->mPendingGets.remove(uid);
      emit guard->serverSnapshotFetchFailed(uid, QStringLiteral("credential_unavailable"));
      return;
    }
    guard->mClient.getSchedule(credential, uid);
  });
}

void ScheduleSync::onGetFinished(const pcm::tokenclient::ScheduleHttpResult &result) {
  const auto pending = mPendingGets.find(result.seriesUid);
  if (pending == mPendingGets.end()) {
    return;
  }
  const auto purpose = pending.value();
  const auto uid = result.seriesUid;
  mPendingGets.erase(pending);

  qint64 revision = 0;
  QString hash;
  QByteArray snapshotJson;
  if (result.ok()) {
    const auto reply = QJsonDocument::fromJson(result.body).object();
    const auto parsedRevision = integerField(reply, "revision");
    if (!parsedRevision.has_value() || !reply.value("snapshot").isObject()) {
      emit serverSnapshotFetchFailed(uid, QStringLiteral("malformed_response"));
      return;
    }
    revision = *parsedRevision;
    hash = reply.value("content_hash").toString();
    snapshotJson = QJsonDocument(reply.value("snapshot").toObject()).toJson(QJsonDocument::Compact);
  } else if (!(purpose == GetPurpose::Publish && result.httpStatus == 404)) {
    emit serverSnapshotFetchFailed(
        uid, !result.errorCode.isEmpty() ? result.errorCode
                                         : QStringLiteral("http_%1").arg(result.httpStatus));
    return;
  }

  if (purpose == GetPurpose::View) {
    emit serverSnapshotFetched(uid, revision, hash, snapshotJson);
    return;
  }

  // Publish: take over the server revision and re-queue the current local
  // schedule on top of it, atomically. A 404 means the server has no copy, so
  // the next write is a first publication (revision 1, base 0).
  const auto identity = mDb.get_schedule_identity_by_uid(uid.toStdString());
  if (!identity.has_value()) {
    emit serverSnapshotFetchFailed(uid, QStringLiteral("unknown_series"));
    return;
  }
  const auto commit = mDb.commit_schedule_change(
      [&]() -> std::optional<int64_t> {
        if (!mDb.adopt_schedule_server_revision(uid.toStdString(), revision, hash.toStdString(),
                                                /*allow_rewind=*/true)) {
          return std::nullopt;
        }
        return identity->series_id;
      },
      {}, buildScheduleSnapshotPayload);
  if (!commit.has_value()) {
    emit serverSnapshotFetchFailed(uid, QStringLiteral("requeue_failed"));
    return;
  }
  auto &runtime = mRuntime[uid];
  runtime.attempts = 0;
  runtime.transient.reset();
  emitStatus(uid);
  pump();
}

} // namespace pcm::meeting
