#pragma once
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QStringList>

class PrompterWorker final : public QObject {
  Q_OBJECT
public:
  explicit PrompterWorker(QObject *parent = nullptr);
  ~PrompterWorker() override;
  bool start(const QString &python, const QString &script, const QString &model, const QString &promptTemplate);
  void stop();
  void send(const QJsonObject &request);
  void consumeOutput(const QByteArray &bytes);
  static QString bundledScript();
signals:
  void ready();
  void resultReady(QJsonObject result);
  void failed();
private:
  void fail();
  QProcess mProcess;
  QTimer mTimeout;
  QByteArray mBuffer;
  QJsonObject mPending;
  QStringList mRestart;
  QJsonObject mExpected;
  bool mReady = false, mStopped = true;
};
