#include "cloud_key_store.h"

#include <keychain.h>

namespace {
constexpr auto kService = "Sessio";
constexpr auto kEntry = "cloud-llm/api-key";
const QString kUnavailable = QStringLiteral("system keychain unavailable");
} // namespace

void CloudKeyStore::read() {
  auto *job = new QKeychain::ReadPasswordJob(QString::fromLatin1(kService), this);
  job->setInsecureFallback(false);
  job->setKey(QString::fromLatin1(kEntry));
  connect(job, &QKeychain::Job::finished, this, [this, job](QKeychain::Job *) {
    if (job->error() == QKeychain::NoError) emit readFinished(true, job->textData(), {});
    else if (job->error() == QKeychain::EntryNotFound) emit readFinished(true, {}, {});
    else emit readFinished(false, {}, kUnavailable);
    job->deleteLater();
  });
  job->start();
}

void CloudKeyStore::write(const QString &key) {
  auto *job = new QKeychain::WritePasswordJob(QString::fromLatin1(kService), this);
  job->setInsecureFallback(false);
  job->setKey(QString::fromLatin1(kEntry));
  job->setTextData(key);
  connect(job, &QKeychain::Job::finished, this, [this, job](QKeychain::Job *) {
    emit writeFinished(job->error() == QKeychain::NoError,
                       job->error() == QKeychain::NoError ? QString{} : kUnavailable);
    job->deleteLater();
  });
  job->start();
}

void CloudKeyStore::remove() {
  auto *job = new QKeychain::DeletePasswordJob(QString::fromLatin1(kService), this);
  job->setInsecureFallback(false);
  job->setKey(QString::fromLatin1(kEntry));
  connect(job, &QKeychain::Job::finished, this, [this, job](QKeychain::Job *) {
    const bool ok = job->error() == QKeychain::NoError || job->error() == QKeychain::EntryNotFound;
    emit writeFinished(ok, ok ? QString{} : kUnavailable);
    job->deleteLater();
  });
  job->start();
}
