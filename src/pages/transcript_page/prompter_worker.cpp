#include "prompter_worker.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QPointer>
#include <QStandardPaths>
#include <atomic>
#include <mutex>
#include <utility>

#include "prompter_pipeline.h"
#include "live_points_pipeline.h"
#include "summary_pipeline.h"
#include "app_settings.h"
#include "cloud_engine.h"
#include "pseudonymizer.h"

Q_LOGGING_CATEGORY(logPrompterWorker, "pcm.prompter.worker")

namespace {
constexpr int kLoadTimeoutMs = 180000; // first load compiles the XNNPACK cache
// Per model call, not per request: one request makes several calls. The
// agreements deadline and the cancellation grace live in PrompterWorker.
constexpr int kNotesCallTimeoutMs = 10 * 60 * 1000;
constexpr qsizetype kMaxRequestBytes = 256 * 1024;
const char *kModelFile = "gemma-4-E4B-it.litertlm";
} // namespace

// State shared with the model thread. The engine is created, used and
// destroyed on that thread; only cancel() is called from the GUI thread.
struct PrompterWorker::Shared {
  std::mutex mutex;
  // Accessed only under mutex, including invokeMethod's receiver lookup. The
  // GUI destructor clears it before QObject destruction can race with posting.
  PrompterWorker *receiver = nullptr;
  std::shared_ptr<pcm::prompter::PrompterEngine> engine;
  // The engine of the request in flight when it is cloud-routed.
  std::shared_ptr<pcm::prompter::PrompterEngine> cloudEngine;
  std::mutex cacheMutex;  // leaf calls may run concurrently
  std::atomic_int generation = 0;
  std::atomic_int requestSerial = 0;
  // Model-thread-only cache of deterministic note subproblems. Finalization
  // reuses unchanged chunks; no source texts or replies are written to logs.
  QHash<QByteArray, QJsonValue> notesCache;
  void cancel() {
    std::lock_guard lock(mutex);
    if (engine) engine->cancel();
    if (cloudEngine) cloudEngine->cancel();
  }
  void setCloud(std::shared_ptr<pcm::prompter::PrompterEngine> next) {
    std::lock_guard lock(mutex);
    cloudEngine = std::move(next);
  }
  std::shared_ptr<pcm::prompter::PrompterEngine> current() {
    std::lock_guard lock(mutex);
    return engine;
  }
  void post(std::function<void(PrompterWorker &)> callback) {
    std::lock_guard lock(mutex);
    if (!receiver) return;
    QMetaObject::invokeMethod(receiver, [target = receiver, callback = std::move(callback)] {
      callback(*target);
    }, Qt::QueuedConnection);
  }
  // Called only on the model thread, including from QThread::finished when
  // quit() bypasses the queued release. Destruction happens outside the lock.
  void release() {
    {
      std::lock_guard lock(cacheMutex);
      notesCache.clear();
    }
    std::shared_ptr<pcm::prompter::PrompterEngine> released;
    {
      std::lock_guard lock(mutex);
      released = std::move(engine);
    }
  }
};

namespace {
QString featureOfTask(const QString &task) {
  return task == "summary" || task == "live_points" ? task : QStringLiteral("agreements");
}
} // namespace

PrompterWorker::PrompterWorker(QObject *parent)
    : PrompterWorker([](const QString &model) {
        const auto cache = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("prompter");
        // Read at every load, so a changed setting applies when the model next starts.
        const auto mode = pcm::app_settings::prompterAcceleration();
        pcm::prompter::GemmaOptions options;
        options.acceleration = mode == "gpu" ? pcm::prompter::GemmaOptions::Acceleration::Gpu
                             : mode == "cpu" ? pcm::prompter::GemmaOptions::Acceleration::Cpu
                                             : pcm::prompter::GemmaOptions::Acceleration::Auto;
        return pcm::prompter::createGemmaEngine(model, cache, options);
      }, parent) {}

PrompterWorker::PrompterWorker(pcm::prompter::PrompterEngineFactory factory, QObject *parent)
    : QObject(parent), mFactory(std::move(factory)), mShared(std::make_shared<Shared>()) {
  mShared->receiver = this;
  mCloud.useCloud = [](const QString &task) { return pcm::app_settings::featureEngine(featureOfTask(task)) == "cloud"; };
  mCloud.pseudonymize = [] { return pcm::app_settings::cloudPseudonymize(); };
  mCloud.label = [] { return QStringLiteral("cloud:") + pcm::app_settings::cloudModel(); };
  mCloud.parallelism = [] { return 4; };
  mCloud.makeEngine = [this]() -> std::unique_ptr<pcm::prompter::PrompterEngine> {
    pcm::prompter::CloudConfig config;
    config.baseUrl = pcm::app_settings::cloudBaseUrl();
    config.model = pcm::app_settings::cloudModel();
    config.apiKey = mCloudApiKey;
    return pcm::prompter::createCloudEngine(config);
  };
  // No GUI owner: synchronous loading can outlive PrompterWorker. Neither the
  // thread nor its callbacks own or dereference that worker after destruction.
  mThread = new QThread;
  mThread->setObjectName("PrompterModel");
  mThreadContext = new QObject;
  mThreadContext->moveToThread(mThread);
  connect(mThread, &QThread::finished, mThreadContext, [shared = mShared] {
    shared->release();
  }, Qt::DirectConnection);
  connect(mThread, &QThread::finished, mThreadContext, &QObject::deleteLater);
  // The GUI event loop must process deferred deletes to reclaim the QThread
  // wrapper. If it has already stopped, the wrapper can survive until process
  // exit; model/context cleanup still runs on the finishing model thread.
  connect(mThread, &QThread::finished, mThread, &QObject::deleteLater);
  mThread->start(QThread::LowPriority);
  mTimeout.setSingleShot(true);
  connect(&mTimeout, &QTimer::timeout, this, &PrompterWorker::timedOut);
}

PrompterWorker::~PrompterWorker() {
  {
    std::lock_guard lock(mShared->mutex);
    mShared->receiver = nullptr;
  }
  stop();
  mThread->quit();
}

QString PrompterWorker::bundledModel() {
  const auto app = QCoreApplication::applicationDirPath();
  const QStringList dirs{QDir(app).filePath("prompter-models"), QDir(app).filePath("../share/sessio/prompter-models"),
                         QDir(app).filePath("../Resources/prompter-models")};
  for (const auto &dir : dirs)
    if (QFileInfo(QDir(dir).filePath(kModelFile)).isFile()) return QFileInfo(QDir(dir).filePath(kModelFile)).absoluteFilePath();
  return {};
}

bool PrompterWorker::start(const QString &model) {
  if (!QFileInfo(model).isFile()) {
    fail(); return false;
  }
  if (!mStopped && !mCloudOnly) return true;
  if (mCloudOnly && !mExpected.isEmpty()) return true;  // a cloud request is running; load later
  mCloudOnly = false;
  mModelPath = model;
  mStopped = false; mReady = false; mTimedOut = false; mPending = {}; mExpected = {}; mQueue.clear(); mReloadAfterCancel = false;
  const int generation = ++mGeneration;
  mShared->generation = generation;
  mTimeout.start(kLoadTimeoutMs);
  QMetaObject::invokeMethod(mThreadContext, [shared = mShared, factory = mFactory, model, generation] {
    if (shared->generation != generation) return;
    std::shared_ptr<pcm::prompter::PrompterEngine> engine;
    try { engine = factory(model); } catch (...) {}
    const bool loaded = engine != nullptr;
    {
      std::lock_guard lock(shared->mutex);
      if (shared->generation == generation) shared->engine = std::move(engine);
    }
    engine.reset(); // a superseded engine is released here, on the model thread
    shared->post([generation, loaded](PrompterWorker &worker) {
      if (generation != worker.mGeneration || worker.mStopped) return;
      if (!loaded) { worker.fail(); return; }
      worker.mReady = true;
      worker.mTimeout.stop();
      // Signal handlers may delete the worker synchronously on this GUI thread.
      QPointer<PrompterWorker> guard(&worker);
      emit worker.ready();
      if (guard && generation == worker.mGeneration && !worker.mStopped && !worker.mPending.isEmpty()) {
        const auto request = worker.mPending;
        worker.mPending = {};
        worker.send(request);
      } else if (guard && generation == worker.mGeneration && !worker.mStopped && worker.mExpected.isEmpty() && !worker.mQueue.isEmpty()) {
        worker.send(worker.mQueue.dequeue());
      }
    });
  }, Qt::QueuedConnection);
  return true;
}

bool PrompterWorker::startCloud() {
  if (!mStopped) return true;
  mStopped = false; mReady = true; mCloudOnly = true; mTimedOut = false;
  mPending = {}; mExpected = {}; mQueue.clear(); mReloadAfterCancel = false; mExpectedCloud = false;
  mShared->generation = ++mGeneration;
  return true;
}

void PrompterWorker::stop() {
  const bool wasActive = !mStopped;
  mCloudOnly = false; mExpectedCloud = false;
  mStopped = true; mReady = false; mTimedOut = false; mTimeout.stop(); mPending = {}; mExpected = {}; mQueue.clear(); mReloadAfterCancel = false;
  ++mShared->requestSerial;
  mShared->generation = ++mGeneration;
  mShared->cancel();
  mShared->setCloud(nullptr);
  QMetaObject::invokeMethod(mThreadContext, [shared = mShared] {
    shared->release();
  }, Qt::QueuedConnection);
  if (wasActive) emit stopped();
}

void PrompterWorker::fail() { stop(); emit failed(); }

void PrompterWorker::timedOut() {
  // A slow call cancels only its request and keeps the model loaded. A load
  // that does not finish, or a call that ignores cancellation, releases it.
  if (!mReady || mExpected.isEmpty() || mTimedOut) {
    qCWarning(logPrompterWorker) << (mReady ? "model call did not stop after cancellation" : "model load timed out");
    fail(); return;
  }
  qCWarning(logPrompterWorker) << mExpected["task"].toString("agreements") << "request timed out";
  mTimedOut = true;
  ++mShared->requestSerial; mShared->cancel();
  mTimeout.start(mCancelGraceMs);
}

void PrompterWorker::progress(int generation, int serial) {
  if (generation == mGeneration && serial == mSerial && !mStopped && !mExpected.isEmpty() && !mTimedOut)
    mTimeout.start(mCallTimeoutMs);
}

namespace {
// Live points and the meeting summary: background work that queues behind
// agreement extraction and may be cancelled per material.
bool isNotesTask(const QJsonObject &request) {
  const auto task = request["task"].toString();
  return task == "live_points" || task == "summary";
}
} // namespace

void PrompterWorker::send(const QJsonObject &request) {
  if (mStopped) { emit failed(); return; }
  if ((!mReady && !mPending.isEmpty()) || (mReady && !mExpected.isEmpty())) {
    if (mQueue.size() >= 8) {
      auto rejected = request; rejected.remove("utterances"); rejected["error"] = "queue_full";
      emit resultReady(rejected); return;
    }
    // Agreement extraction stays ahead of background note updates.
    if (!isNotesTask(request)) mQueue.prepend(request); else mQueue.enqueue(request);
    return;
  }
  if (!mReady) {
    mPending = request; return;
  }
  if (QJsonDocument(request).toJson(QJsonDocument::Compact).size() > kMaxRequestBytes) { fail(); return; }
  mExpected = request;
  const auto task = request["task"].toString();
  const bool cloud = mCloud.useCloud && mCloud.useCloud(task);
  mExpectedCloud = cloud;
  const auto failNow = [this, &request](const char *reason, const char *detail) {
    QJsonObject result{{"protocol_version", 1}, {"error", "inference_failed"}, {"reason", reason}};
    if (detail) result.insert("cloud_detail", detail);
    for (const char *key : {"request_id", "session_generation", "ledger_revision", "task", "material_id", "final", "request_token"})
      if (request.contains(key)) result.insert(key, request.value(key));
    // Queued, like every other result: the caller may not be listening yet.
    QMetaObject::invokeMethod(this, [this, generation = mGeneration, result] { finish(generation, result); }, Qt::QueuedConnection);
  };
  std::shared_ptr<pcm::prompter::PrompterEngine> cloudEngine;
  if (cloud) {
    if (mCloud.makeEngine) cloudEngine = mCloud.makeEngine();
    if (!cloudEngine) { failNow("cloud", "no_engine"); return; }
    mShared->setCloud(cloudEngine);
  } else if (mCloudOnly) {
    failNow("runtime", nullptr); return;
  }
  const bool pseudonymize = cloud && (!mCloud.pseudonymize || mCloud.pseudonymize());
  const int parallelism = cloud && mCloud.parallelism ? std::max(1, mCloud.parallelism()) : 1;
  const QString engineLabel = cloud ? (mCloud.label ? mCloud.label() : QStringLiteral("cloud")) : QStringLiteral("local");
  mCallTimeoutMs = isNotesTask(request) ? kNotesCallTimeoutMs : mAgreementsCallMs;
  mTimeout.start(mCallTimeoutMs);
  const int generation = mGeneration;
  const int serial = mSerial = ++mShared->requestSerial;
  QMetaObject::invokeMethod(mThreadContext, [shared = mShared, request, generation, serial, cloud, cloudEngine,
                                              pseudonymize, parallelism, engineLabel] {
    auto engine = cloud ? cloudEngine : shared->current();
    if (!engine || shared->generation != generation) return;
    QJsonObject result;
    QList<QByteArray> cached; // replies cached by this request; dropped if it fails
    std::mutex cachedMutex;
    std::shared_ptr<pcm::prompter::Pseudonymizer> pseudonyms;
    std::mutex pseudonymMutex;  // leaf calls may run concurrently
    if (pseudonymize) {
      pcm::prompter::PseudonymSubjects subjects;
      const auto subjectsObject = request["subjects"].toObject();
      const auto list = [&subjectsObject](const char *key) {
        QStringList out;
        for (const auto &value : subjectsObject[key].toArray()) out << value.toString();
        return out;
      };
      subjects.clientNames = list("client"); subjects.specialistNames = list("specialist");
      subjects.phones = list("phones"); subjects.emails = list("emails");
      pseudonyms = std::make_shared<pcm::prompter::Pseudonymizer>(subjects);
    }
    try {
      auto generate = [&engine, &cached, &cachedMutex, &pseudonyms, &pseudonymMutex, shared, generation, serial, cloud,
                       notes = isNotesTask(request)](const QString &system, const QByteArray &payload, const QByteArray &schema) {
        if (shared->generation != generation || shared->requestSerial != serial)
          throw pcm::prompter::PipelineError("cancelled");
        const auto key = QCryptographicHash::hash(system.toUtf8() + QByteArray(1, '\0') + payload + QByteArray(1, '\0') + schema +
                                                  QByteArray(1, cloud ? 'c' : 'l'), QCryptographicHash::Sha256);
        if (notes) {
          std::lock_guard lock(shared->cacheMutex);
          if (shared->notesCache.contains(key)) return shared->notesCache.value(key);
        }
        shared->post([generation, serial](PrompterWorker &worker) { worker.progress(generation, serial); });
        QByteArray outgoing = payload;
        if (pseudonyms) { std::lock_guard lock(pseudonymMutex); outgoing = pseudonyms->applyJson(payload); }
        auto value = engine->generate(system, outgoing, schema);
        if (pseudonyms) { std::lock_guard lock(pseudonymMutex); value = pseudonyms->restore(value); }
        if (shared->generation != generation || shared->requestSerial != serial)
          throw pcm::prompter::PipelineError("cancelled");
        if (notes) {
          std::lock_guard lock(shared->cacheMutex);
          if (shared->notesCache.size() >= 128) shared->notesCache.erase(shared->notesCache.begin());
          shared->notesCache.insert(key, value);
          std::lock_guard cachedLock(cachedMutex);
          cached.append(key);
        }
        return value;
      };
      if (isNotesTask(request)) {
        QJsonObject notes;
        if (request["task"] == "live_points") {
          QStringList known;
          for (const auto &text : request["known_points"].toArray()) known << text.toString();
          notes = pcm::prompter::processLivePoints(request["utterances"].toArray(), known, generate);
        } else if (request["task"] == "summary") {
          pcm::prompter::SummaryContext context{QDate::fromString(request["call_date"].toString(), Qt::ISODate), {}};
          for (const auto &text : request["agreements"].toArray()) context.agreements << text.toString();
          context.parallelism = parallelism;
          notes = pcm::prompter::processSummary(request["utterances"].toArray(), context, generate);
        } else {
          throw pcm::prompter::PipelineError("unsupported notes task");
        }
        result = {{"protocol_version", 1}, {"notes", notes}};
        for (const char *key : {"request_id", "session_generation", "ledger_revision", "task", "material_id", "final", "request_token"})
          result.insert(key, request.value(key));
      } else result = pcm::prompter::processRequest(request, generate);
      result.insert("engine", engineLabel);
    } catch (...) {
      // Greedy decoding repeats a rejected reply for the same input, so a
      // cached copy would make every retry fail the same way.
      {
        std::lock_guard lock(shared->cacheMutex);
        for (const auto &key : cached) shared->notesCache.remove(key);
      }
      // PipelineError texts are fixed codes in src/call_prompter; anything
      // else may describe source text or prompts and is never logged.
      QString code = QStringLiteral("unexpected");
      try { throw; } catch (const pcm::prompter::PipelineError &error) { code = QString::fromLatin1(error.what()); } catch (...) {}
      qCWarning(logPrompterWorker) << request["task"].toString("agreements") << "request failed:" << code;
      result = {{"protocol_version", 1}, {"error", "inference_failed"}, {"reason", pcm::prompter::failureReason(code)}};
      if (cloud && code.startsWith(QLatin1String("cloud "))) result.insert("cloud_detail", code.mid(6).replace(' ', '_'));
      for (const char *key : {"request_id", "session_generation", "ledger_revision", "task", "material_id", "final", "request_token"})
        if (request.contains(key)) result.insert(key, request.value(key));
    }
    shared->post([generation, result](PrompterWorker &worker) { worker.finish(generation, result); });
  }, Qt::QueuedConnection);
}

void PrompterWorker::finish(int generation, const QJsonObject &result) {
  if (generation != mGeneration || mStopped) return;
  if (mExpected.isEmpty() || result["request_id"] != mExpected["request_id"] ||
      result["session_generation"] != mExpected["session_generation"] ||
      result["ledger_revision"] != mExpected["ledger_revision"]) { fail(); return; }
  mExpected = {}; mTimeout.stop();
  mShared->setCloud(nullptr);
  auto reply = result;
  if (std::exchange(mTimedOut, false) && reply.contains("error")) reply["reason"] = QStringLiteral("timeout");
  if (mReloadAfterCancel) {
    // Cancellation may arrive after the model posted its reply. Decide here
    // on the GUI thread, where cancelNotes and queue ownership are serialized.
    // Release and reload before draining; never load a second model alongside
    // the first, and do not report unrelated queued requests as stopped.
    auto queued = std::move(mQueue);
    mStopped = true; stop();
    if (start(mModelPath)) mQueue = std::move(queued);
    emit resultReady(reply);
    return;
  }
  QPointer<PrompterWorker> guard(this);
  emit resultReady(reply);
  if (guard && !mStopped && mExpected.isEmpty() && !mQueue.isEmpty()) send(mQueue.dequeue());
}

void PrompterWorker::cancelNotes(qint64 materialId, const QString &requestToken) {
  const auto matches = [materialId, &requestToken](const QJsonObject &request) {
    return isNotesTask(request) && request["material_id"].toInteger() == materialId &&
        (requestToken.isEmpty() || request["request_token"].toString() == requestToken);
  };
  for (qsizetype i = mQueue.size(); i > 0; --i)
    if (matches(mQueue[i - 1])) mQueue.removeAt(i - 1);
  if (matches(mPending)) mPending = {};
  if (matches(mExpected)) {
    // A cloud request leaves the local model alone; only a local one reloads it.
    mReloadAfterCancel = !mExpectedCloud;
    ++mShared->requestSerial; mShared->cancel();
  }
}
