#include "token_backend_credential_store.h"

#include <keychain.h>

namespace {
constexpr auto kKeychainService = "Sessio";
constexpr auto kKeychainKey = "token-backend/bearer-credential";
}

QtKeychainTokenBackendCredentialStore::QtKeychainTokenBackendCredentialStore(QObject *parent)
    : TokenBackendCredentialStore(parent) {}

void QtKeychainTokenBackendCredentialStore::readBearerCredential() {
  auto *job = new QKeychain::ReadPasswordJob(QString::fromLatin1(kKeychainService), this);
  job->setInsecureFallback(false);
  job->setKey(QString::fromLatin1(kKeychainKey));
  connect(job, &QKeychain::Job::finished, this, [this, job](QKeychain::Job *) {
    if (job->error() != QKeychain::NoError) {
      emit readFinished(false, {}, QStringLiteral("system keychain unavailable"));
    } else {
      emit readFinished(true, job->textData(), {});
    }
    job->deleteLater();
  });
  job->start();
}

void QtKeychainTokenBackendCredentialStore::writeBearerCredential(const QString &credential) {
  auto *job = new QKeychain::WritePasswordJob(QString::fromLatin1(kKeychainService), this);
  job->setInsecureFallback(false);
  job->setKey(QString::fromLatin1(kKeychainKey));
  job->setTextData(credential);
  connect(job, &QKeychain::Job::finished, this, [this, job](QKeychain::Job *) {
    emit writeFinished(job->error() == QKeychain::NoError,
                       job->error() == QKeychain::NoError
                           ? QString{}
                           : QStringLiteral("system keychain unavailable"));
    job->deleteLater();
  });
  job->start();
}
