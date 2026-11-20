#include "keychain_series_invitation_store.h"

#include <keychain.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

namespace {
constexpr auto kKeychainService = "Sessio";

QString keyFor(const QString &seriesUid) {
  return QStringLiteral("series-invitation/") + seriesUid.toLower();
}
} // namespace

QtKeychainSeriesInvitationStore::QtKeychainSeriesInvitationStore(QObject *parent)
    : pcm::meeting::SeriesInvitationSecretStore(parent) {}

void QtKeychainSeriesInvitationStore::read(const QString &seriesUid, const ReadCallback &done) {
  auto *job = new QKeychain::ReadPasswordJob(QString::fromLatin1(kKeychainService), this);
  job->setInsecureFallback(false);
  job->setKey(keyFor(seriesUid));
  connect(job, &QKeychain::Job::finished, this, [job, done](QKeychain::Job *) {
    job->deleteLater();
    if (job->error() == QKeychain::EntryNotFound) {
      done(true, std::nullopt);
      return;
    }
    if (job->error() != QKeychain::NoError) {
      done(false, std::nullopt);
      return;
    }
    const auto object = QJsonDocument::fromJson(job->textData().toUtf8()).object();
    pcm::meeting::SeriesInvitationSecret secret;
    secret.url = object.value("url").toString();
    secret.passcode = object.value("passcode").toString();
    secret.generation = static_cast<qint64>(object.value("generation").toDouble(0));
    if (secret.url.isEmpty() || secret.passcode.isEmpty()) {
      done(true, std::nullopt); // unreadable value counts as "nothing stored"
      return;
    }
    done(true, secret);
  });
  job->start();
}

void QtKeychainSeriesInvitationStore::write(const QString &seriesUid,
                                            const pcm::meeting::SeriesInvitationSecret &secret,
                                            const DoneCallback &done) {
  auto *job = new QKeychain::WritePasswordJob(QString::fromLatin1(kKeychainService), this);
  job->setInsecureFallback(false);
  job->setKey(keyFor(seriesUid));
  job->setTextData(QString::fromUtf8(
      QJsonDocument(QJsonObject{{"url", secret.url},
                                {"passcode", secret.passcode},
                                {"generation", static_cast<double>(secret.generation)}})
          .toJson(QJsonDocument::Compact)));
  connect(job, &QKeychain::Job::finished, this, [job, done](QKeychain::Job *) {
    job->deleteLater();
    done(job->error() == QKeychain::NoError);
  });
  job->start();
}

void QtKeychainSeriesInvitationStore::remove(const QString &seriesUid, const DoneCallback &done) {
  auto *job = new QKeychain::DeletePasswordJob(QString::fromLatin1(kKeychainService), this);
  job->setInsecureFallback(false);
  job->setKey(keyFor(seriesUid));
  connect(job, &QKeychain::Job::finished, this, [job, done](QKeychain::Job *) {
    job->deleteLater();
    done(job->error() == QKeychain::NoError || job->error() == QKeychain::EntryNotFound);
  });
  job->start();
}
