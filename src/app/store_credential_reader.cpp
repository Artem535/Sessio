#include "store_credential_reader.h"

#include <QPointer>

StoreCredentialReader::StoreCredentialReader(TokenBackendCredentialStore &store, QObject *parent)
    : QObject(parent), mStore(store) {
  connect(&mStore, &TokenBackendCredentialStore::readFinished, this,
          &StoreCredentialReader::onReadFinished);
}

pcm::meeting::ScheduleSync::CredentialReader StoreCredentialReader::reader() {
  QPointer<StoreCredentialReader> guard(this);
  return [guard](const pcm::meeting::ScheduleSync::CredentialCallback &done) {
    if (!guard) {
      done(false, {});
      return;
    }
    guard->read(done);
  };
}

void StoreCredentialReader::read(const pcm::meeting::ScheduleSync::CredentialCallback &done) {
  // Concurrent callers share one keychain read; each still gets its own answer.
  mWaiting.append(done);
  if (mReadInFlight) {
    return;
  }
  mReadInFlight = true;
  mStore.readBearerCredential();
}

void StoreCredentialReader::onReadFinished(const bool ok, const QString &credential, const QString &) {
  if (!mReadInFlight) {
    return; // someone else's read (e.g. the settings refresh)
  }
  mReadInFlight = false;
  const auto waiting = std::exchange(mWaiting, {});
  const bool usable = ok && !credential.isEmpty();
  for (const auto &callback : waiting) {
    callback(usable, usable ? credential : QString());
  }
}
