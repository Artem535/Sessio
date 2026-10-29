#pragma once
#include <QJsonObject>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QQueue>
#include <functional>
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
  // Cloud-routed requests only: no local model is loaded, a request routed to
  // the local engine fails with reason "runtime". A later start() loads one.
  bool startCloud();
  // Which engine serves a request. By default the user's settings decide
  // (featureEngine per task) and the engine is built from the cloud settings
  // and the in-memory API key (app_settings::cloudApiKeyCached). Tests replace all of it.
  struct CloudRouting {
    std::function<bool(const QString &task)> useCloud;
    std::function<std::unique_ptr<pcm::prompter::PrompterEngine>()> makeEngine;  // GUI thread, per request
    std::function<bool()> pseudonymize;
    std::function<QString()> label;  // "cloud:<model>" in results
    std::function<int()> parallelism;  // concurrent summary leaf calls
  };
  void setCloudRouting(CloudRouting routing) { mCloud = std::move(routing); }
  void stop();
  void send(const QJsonObject &request);
  void cancelNotes(qint64 materialId, const QString &requestToken = {});
  // Tests shorten the agreements call deadline and the cancellation grace.
  void setTimeouts(int agreementsCallMs, int cancelGraceMs) { mAgreementsCallMs = agreementsCallMs; mCancelGraceMs = cancelGraceMs; }
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
  void timedOut();
  void progress(int generation, int serial);
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
  CloudRouting mCloud;
  bool mCloudOnly = false, mExpectedCloud = false;
  int mGeneration = 0, mSerial = 0, mCallTimeoutMs = 0;
  int mAgreementsCallMs = 120000, mCancelGraceMs = 30000;
  bool mReady = false, mStopped = true, mTimedOut = false;
  bool mReloadAfterCancel = false;
};
