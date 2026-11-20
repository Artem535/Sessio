#pragma once

#include "token_backend_credential_store.h"

// In-memory test double — never touches the real OS keychain, matching the
// same pattern used to test consumers of CredentialStore in backup_tests.cpp.
class FakeTokenBackendCredentialStore final : public TokenBackendCredentialStore {
  Q_OBJECT

public:
  using TokenBackendCredentialStore::TokenBackendCredentialStore;

  void readBearerCredential() override {
    emit readFinished(mHasCredential, mCredential, mHasCredential ? QString{} : QStringLiteral("not_set"));
  }

  void writeBearerCredential(const QString &credential) override {
    mCredential = credential;
    mHasCredential = true;
    emit writeFinished(true, {});
  }

  QString mCredential;
  bool mHasCredential = false;
};
