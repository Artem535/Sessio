#pragma once

#include "schedule_sync.h"
#include "series_invitation_store.h"

#include <QHash>
#include <QObject>
#include <QString>

#include <functional>

class QTimer;

namespace pcm::meeting {

enum class InvitationState {
  None,               // no invitation requested yet
  WaitingForSchedule, // schedule not acknowledged by the server yet: nothing is requested
  Requesting,         // POST /invitation in flight
  StoringSecret,      // server answered; the secret is being written to secure storage
  Ready,              // permanent invitation exists and its secret is stored
  WaitingForNetwork,  // offline/5xx/timeout: retrying with the same idempotency key
  NeedsReissue,       // server holds an invitation whose secret this device lacks
  Failed              // see detail; manual retry
};

struct InvitationStatus {
  InvitationState state = InvitationState::None;
  QString detail; // machine-readable code, never a secret
  qint64 generation = 0;
};

// Creates and keeps the ONE permanent invitation of a published series.
//
// - Nothing is requested until the server acknowledged the schedule (no
//   invitation for a schedule the server does not know yet).
// - The request is idempotent: the key is persisted, so a lost response is
//   replayed with the same key instead of creating a second invitation.
// - The secret goes to secure storage first; only then are the generation
//   recorded and the persisted key dropped. If the storage fails the key is
//   kept, so retrying within the server's replay window returns the same secret.
// - Editing the schedule never touches an existing invitation. A new one only
//   exists after an explicit reissue().
class SeriesInvitationService final : public QObject {
  Q_OBJECT

public:
  SeriesInvitationService(pcm::database::Database &db, ScheduleSync &sync,
                          pcm::tokenclient::TokenBackendClient &client,
                          ScheduleSync::CredentialReader credentialReader,
                          SeriesInvitationSecretStore &store, QObject *parent = nullptr);
  ~SeriesInvitationService() override;

  // Test hook; defaults to ScheduleSync::retryDelayMs with random jitter.
  void setBackoffPolicy(std::function<int(int attempt)> policy) { mBackoff = std::move(policy); }

  // Makes sure the series has an invitation: no-op when one exists, waits for
  // the schedule ACK first. Safe to call repeatedly.
  void ensureInvitation(int64_t seriesId);
  // Explicitly replaces the invitation (the old generation stops working).
  void reissueInvitation(int64_t seriesId);
  // Manual retry after Failed / NeedsReissue-less errors; repeats the last intent.
  void retry(int64_t seriesId);

  [[nodiscard]] InvitationStatus status(int64_t seriesId);
  // Reads the stored secret asynchronously (for "copy invitation"). `ok` false:
  // secure storage unavailable; ok with empty url: no secret stored here.
  using LoadCallback = std::function<void(bool ok, const QString &url, const QString &passcode)>;
  void loadInvitation(int64_t seriesId, const LoadCallback &done);

signals:
  void statusChanged(qint64 seriesId, pcm::meeting::InvitationStatus status);
  void invitationReady(qint64 seriesId);

private:
  struct Runtime {
    InvitationState state = InvitationState::None;
    QString detail;
    QString uid;
    bool wanted = false;
    bool reissue = false;
    bool inflight = false;
    int attempts = 0;
    QTimer *timer = nullptr;
  };

  void request(int64_t seriesId, bool reissue);
  void send(int64_t seriesId);
  void onInvitationFinished(const pcm::tokenclient::ScheduleHttpResult &result);
  void onScheduleStatus(const QString &uid);
  void store(int64_t seriesId, const SeriesInvitationSecret &secret);
  void setState(int64_t seriesId, InvitationState state, const QString &detail = {});
  void scheduleRetry(int64_t seriesId, int minDelayMs);

  pcm::database::Database &mDb;
  ScheduleSync &mSync;
  pcm::tokenclient::TokenBackendClient &mClient;
  ScheduleSync::CredentialReader mReadCredential;
  SeriesInvitationSecretStore &mStore;
  std::function<int(int)> mBackoff;
  QHash<qint64, Runtime> mRuntime;
};

} // namespace pcm::meeting

Q_DECLARE_METATYPE(pcm::meeting::InvitationStatus)
