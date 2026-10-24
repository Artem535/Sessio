#pragma once
#include <QJsonObject>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QQueue>
#include <memory>

#include "prompter_engine.h"

// Runs the local prompter model inside Sessio on a dedicated thread, never on
// the GUI thread. One request is processed at a time; stop() cancels it,
// discards its result and releases the model memory.
// Destruction does not join the model thread: a synchronous model load cannot
// be interrupted and must finish before its thread and model can be released.
class PrompterWorker final : public QObject {
  Q_OBJECT
public:
  explicit PrompterWorker(QObject *parent = nullptr);
  explicit PrompterWorker(pcm::prompter::PrompterEngineFactory factory, QObject *parent = nullptr);
  ~PrompterWorker() override;
  bool start(const QString &model);
  void stop();
  void send(const QJsonObject &request);
  void cancelNotes(qint64 materialId, const QString &requestToken = {});
  // The Gemma model shipped next to the application, or empty.
  static QString bundledModel();
signals:
  void ready();
  void resultReady(QJsonObject result);
  void failed();
  void stopped();
private:
  struct Shared;
  void fail();
  void finish(int generation, const QJsonObject &result);
  pcm::prompter::PrompterEngineFactory mFactory;
  std::shared_ptr<Shared> mShared;
  QThread *mThread = nullptr;
  QObject *mThreadContext = nullptr;
  QTimer mTimeout;
  QJsonObject mPending;
  QJsonObject mExpected;
  QQueue<QJsonObject> mQueue;
  QString mModelPath;
  int mGeneration = 0;
  bool mReady = false, mStopped = true;
  bool mReloadAfterCancel = false;
};
