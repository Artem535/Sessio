#pragma once

#include "schedule_sync.h"
#include "token_backend_credential_store.h"

#include <QList>
#include <QObject>

// Adapts the asynchronous TokenBackendCredentialStore (OS keychain) to the
// CredentialReader the schedule services use. The bearer credential is read
// from the store for every request, handed to that request's callback and
// forgotten: it is never cached here, and nothing is read at construction, so
// a keychain that is locked or slow at start-up cannot leave the services with
// a permanently empty credential.
class StoreCredentialReader final : public QObject {
  Q_OBJECT

public:
  explicit StoreCredentialReader(TokenBackendCredentialStore &store, QObject *parent = nullptr);

  // Callable that outlives nothing it should not: it holds only a guarded
  // pointer to this adapter and does nothing once the adapter is gone.
  [[nodiscard]] pcm::meeting::ScheduleSync::CredentialReader reader();

private slots:
  void onReadFinished(bool ok, const QString &credential, const QString &error);

private:
  void read(const pcm::meeting::ScheduleSync::CredentialCallback &done);

  TokenBackendCredentialStore &mStore;
  QList<pcm::meeting::ScheduleSync::CredentialCallback> mWaiting;
  bool mReadInFlight = false;
};
