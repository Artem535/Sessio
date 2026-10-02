#pragma once

#include "database.h"
#include "token_backend_client.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class QTimer;

namespace pcm::meeting {

// What the UI shows for one series. "Sending" and "WaitingForNetwork" are
// transient; Conflict and Rejected are persisted and pause automatic retries.
enum class ScheduleSyncState {
  Synced,               // server acknowledged the latest local schedule
  Queued,               // changes waiting to be sent
  Sending,              // a request owns the immutable in-flight payload
  WaitingForNetwork,    // offline/timeout/5xx/429: retrying with backoff
  Conflict,             // server revision differs; queue stopped, manual action needed
  Rejected,             // server refused the content (413/422...); no automatic retry
  Unsupported,          // backend lacks the schedule-series API; nothing is published
  Unauthorized,         // credential refused (401); waits for wake()/retry()
  CredentialUnavailable // secure storage could not provide the credential
};

enum class ScheduleCapability { Unknown, Checking, Supported, Unsupported, Unauthorized, Unreachable };

struct ScheduleSyncStatus {
  ScheduleSyncState state = ScheduleSyncState::Synced;
  qint64 ackedRevision = 0;
  qint64 desiredRevision = 0;
  // True while the server may still enforce an older schedule than the local
  // one (anything but Synced), e.g. after a local cancellation without ACK.
  bool unconfirmed = false;
  QString detail; // machine-readable code such as "revision_conflict"; no user data
};

// Asynchronous publisher of local recurring schedules. Local edits are already
// durable in the database outbox (Database::commit_schedule_change); this
// service only delivers them: one immutable in-flight payload per series,
// byte-identical resend after a lost ACK or restart, never blocks the event
// loop, and every signal names the series UUID.
class ScheduleSync final : public QObject {
  Q_OBJECT

public:
  using CredentialCallback = std::function<void(bool ok, const QString &credential)>;
  // Fetches the bearer credential from secure storage and calls back, possibly
  // later. The credential is never cached or logged by this class.
  using CredentialReader = std::function<void(const CredentialCallback &)>;

  ScheduleSync(pcm::database::Database &db, pcm::tokenclient::TokenBackendClient &client,
               CredentialReader credentialReader, QObject *parent = nullptr);
  ~ScheduleSync() override;

  // Delay before attempt number `attempt` (0-based): 2, 5, 15, 60 seconds, then
  // 5 minutes; `jitterUnit` in [0,1) spreads it by +-20%, never above 5 minutes.
  [[nodiscard]] static int retryDelayMs(int attempt, double jitterUnit);
  // Test hook; the default policy is retryDelayMs with random jitter.
  void setBackoffPolicy(std::function<int(int attempt)> policy) { mBackoff = std::move(policy); }

  // Recovers the persisted queue after restart, probes the backend once and
  // drains. Safe to call repeatedly.
  void start();
  // A local change was committed for this series (queue already durable).
  void notifyLocalChange(const QString &seriesUid);
  // Network came back / user pressed retry for everything: cancels backoff
  // waits and re-probes an unsupported or unauthorized backend.
  void wake();
  // Manual retry of one series after Rejected, CredentialUnavailable, etc. It
  // never resumes a Conflict; use publishRestoredSchedule for that.
  void retry(const QString &seriesUid);
  // Reads the server's current snapshot for review. Changes nothing.
  void fetchServerSnapshot(const QString &seriesUid);
  // Explicit "publish restored schedule": adopts the server's current revision
  // and re-queues the local state on top of it. Only call after the user has
  // reviewed the difference; never done automatically.
  void publishRestoredSchedule(const QString &seriesUid);

  [[nodiscard]] ScheduleSyncStatus status(const QString &seriesUid);
  [[nodiscard]] ScheduleCapability capability() const { return mCapability; }

signals:
  void statusChanged(const QString &seriesUid, pcm::meeting::ScheduleSyncStatus status);
  // The server acknowledged `revision` for the series (newer edits may remain).
  void synced(const QString &seriesUid, qint64 revision);
  void capabilityChanged(pcm::meeting::ScheduleCapability capability);
  void serverSnapshotFetched(const QString &seriesUid, qint64 revision,
                             const QString &contentHash, const QByteArray &snapshotJson);
  void serverSnapshotFetchFailed(const QString &seriesUid, const QString &reason);

private:
  struct Runtime {
    std::optional<ScheduleSyncState> transient;
    QString detail;
    int attempts = 0;
    QTimer *timer = nullptr;
  };
  struct Active {
    QString uid;
    qint64 revision = 0;
    quint64 token = 0;
    std::string hash;
  };
  enum class GetPurpose { View, Publish };

  void pump();
  void probeCapability();
  void onCapabilities(const pcm::tokenclient::ScheduleHttpResult &result);
  void send(const pcm::database::ScheduleIdentity &identity);
  void onPutFinished(const pcm::tokenclient::ScheduleHttpResult &result);
  void onGetFinished(const pcm::tokenclient::ScheduleHttpResult &result);
  void scheduleRetry(const QString &uid, int minDelayMs);
  void scheduleProbeRetry();
  void setTransient(const QString &uid, std::optional<ScheduleSyncState> state,
                    const QString &detail = {});
  void emitStatus(const QString &uid);
  void emitAllStatuses();
  void requestGet(const QString &uid, GetPurpose purpose);
  [[nodiscard]] int nextDelayMs(int attempt) const;

  pcm::database::Database &mDb;
  pcm::tokenclient::TokenBackendClient &mClient;
  CredentialReader mReadCredential;
  std::function<int(int)> mBackoff;

  QHash<QString, Runtime> mRuntime;
  QHash<QString, GetPurpose> mPendingGets;
  std::optional<Active> mActive;
  quint64 mNextToken = 0;
  ScheduleCapability mCapability = ScheduleCapability::Unknown;
  bool mCredentialBlocked = false; // capability probe could not read the credential
  int mProbeAttempts = 0;
  QTimer *mProbeTimer = nullptr;
  bool mStarted = false;
  bool mPumpQueued = false;
};

} // namespace pcm::meeting

Q_DECLARE_METATYPE(pcm::meeting::ScheduleSyncStatus)
Q_DECLARE_METATYPE(pcm::meeting::ScheduleCapability)
