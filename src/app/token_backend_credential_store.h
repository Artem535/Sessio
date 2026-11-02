#pragma once

#include <QObject>
#include <QString>

class TokenBackendCredentialStore : public QObject {
  Q_OBJECT

public:
  using QObject::QObject;
  ~TokenBackendCredentialStore() override = default;

  virtual void readBearerCredential() = 0;
  virtual void writeBearerCredential(const QString &credential) = 0;

signals:
  void readFinished(bool ok, QString credential, QString error);
  void writeFinished(bool ok, QString error);
};

class QtKeychainTokenBackendCredentialStore final : public TokenBackendCredentialStore {
  Q_OBJECT

public:
  explicit QtKeychainTokenBackendCredentialStore(QObject *parent = nullptr);

  void readBearerCredential() override;
  void writeBearerCredential(const QString &credential) override;
};
