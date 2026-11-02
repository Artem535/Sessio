#pragma once

#include "series_invitation_store.h"

#include <QHash>

// In-memory test double: never touches the OS keychain.
class FakeSeriesInvitationStore final : public pcm::meeting::SeriesInvitationSecretStore {
  Q_OBJECT

public:
  using pcm::meeting::SeriesInvitationSecretStore::SeriesInvitationSecretStore;
  using Secret = pcm::meeting::SeriesInvitationSecret;

  void read(const QString &uid, const ReadCallback &done) override {
    ++reads;
    if (!available) {
      done(false, std::nullopt);
      return;
    }
    done(true, secrets.contains(uid) ? std::optional<Secret>(secrets.value(uid)) : std::nullopt);
  }
  void write(const QString &uid, const Secret &secret, const DoneCallback &done) override {
    ++writes;
    if (!available) {
      done(false);
      return;
    }
    secrets.insert(uid, secret);
    done(true);
  }
  void remove(const QString &uid, const DoneCallback &done) override {
    secrets.remove(uid);
    done(available);
  }

  bool available = true;
  int reads = 0;
  int writes = 0;
  QHash<QString, Secret> secrets;
};
