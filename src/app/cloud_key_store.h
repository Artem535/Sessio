#pragma once

#include <QObject>
#include <QString>

// The cloud LLM API key, in the system keychain and nowhere else. Jobs are
// asynchronous; a missing entry reads as ok with an empty key.
class CloudKeyStore : public QObject {
  Q_OBJECT

public:
  explicit CloudKeyStore(QObject *parent = nullptr) : QObject(parent) {}
  void read();
  void write(const QString &key);
  void remove();

signals:
  void readFinished(bool ok, QString key, QString error);
  void writeFinished(bool ok, QString error);
};
