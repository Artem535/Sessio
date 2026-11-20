#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <optional>

namespace pcm::meeting {

// The permanent invitation of a published series: link plus passcode. This is
// a secret; it lives only in secure storage (never in the local database, logs
// or backups) and is always read and written asynchronously.
struct SeriesInvitationSecret {
  QString url;
  QString passcode;
  qint64 generation = 0;
};

class SeriesInvitationSecretStore : public QObject {
  Q_OBJECT

public:
  using QObject::QObject;
  ~SeriesInvitationSecretStore() override = default;

  // `ok` is false when secure storage is unavailable (locked keychain...).
  // ok with an empty secret means nothing is stored for the series.
  using ReadCallback = std::function<void(bool ok, std::optional<SeriesInvitationSecret> secret)>;
  using DoneCallback = std::function<void(bool ok)>;

  virtual void read(const QString &seriesUid, const ReadCallback &done) = 0;
  virtual void write(const QString &seriesUid, const SeriesInvitationSecret &secret,
                     const DoneCallback &done) = 0;
  virtual void remove(const QString &seriesUid, const DoneCallback &done) = 0;
};

} // namespace pcm::meeting
