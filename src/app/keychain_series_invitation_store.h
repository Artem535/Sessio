#pragma once

#include "series_invitation_store.h"

// Keeps each series' invitation link and passcode in the OS keychain
// (service "Sessio", key "series-invitation/<uid>"), as a small JSON value.
class QtKeychainSeriesInvitationStore final : public pcm::meeting::SeriesInvitationSecretStore {
  Q_OBJECT

public:
  explicit QtKeychainSeriesInvitationStore(QObject *parent = nullptr);

  void read(const QString &seriesUid, const ReadCallback &done) override;
  void write(const QString &seriesUid, const pcm::meeting::SeriesInvitationSecret &secret,
             const DoneCallback &done) override;
  void remove(const QString &seriesUid, const DoneCallback &done) override;
};
