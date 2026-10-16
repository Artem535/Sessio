#include "prompter_worker.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <atomic>
#include <mutex>

#include "prompter_pipeline.h"

namespace {
constexpr int kLoadTimeoutMs = 180000; // first load compiles the XNNPACK cache
constexpr int kRequestTimeoutMs = 90000;
constexpr qsizetype kMaxRequestBytes = 256 * 1024;
const char *kModelFile = "gemma-4-E4B-it.litertlm";
} // namespace

// State shared with the model thread. The engine is created, used and
// destroyed on that thread; only cancel() is called from the GUI thread.
struct PrompterWorker::Shared {
  std::mutex mutex;
  std::shared_ptr<pcm::prompter::PrompterEngine> engine;
  std::atomic_int generation = 0;
  void cancel() {
    std::lock_guard lock(mutex);
    if (engine) engine->cancel();
  }
  std::shared_ptr<pcm::prompter::PrompterEngine> current() {
    std::lock_guard lock(mutex);
    return engine;
  }
};

PrompterWorker::PrompterWorker(QObject *parent)
    : PrompterWorker([](const QString &model) {
        const auto cache = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("prompter");
        return pcm::prompter::createGemmaEngine(model, cache);
      }, parent) {}

PrompterWorker::PrompterWorker(pcm::prompter::PrompterEngineFactory factory, QObject *parent)
    : QObject(parent), mFactory(std::move(factory)), mShared(std::make_shared<Shared>()) {
  mThread.setObjectName("PrompterModel");
  mThreadContext = new QObject;
  mThreadContext->moveToThread(&mThread);
  connect(&mThread, &QThread::finished, mThreadContext, &QObject::deleteLater);
  mThread.start(QThread::LowPriority);
  mTimeout.setSingleShot(true);
  connect(&mTimeout, &QTimer::timeout, this, &PrompterWorker::fail);
}

PrompterWorker::~PrompterWorker() {
  stop();
  mThread.quit();
  mThread.wait();
  // The thread may exit before the queued release ran; nothing runs the model now.
  std::lock_guard lock(mShared->mutex);
  mShared->engine.reset();
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
  if (!mStopped) return true;
  mStopped = false; mReady = false; mPending = {}; mExpected = {};
  const int generation = ++mGeneration;
  mShared->generation = generation;
  mTimeout.start(kLoadTimeoutMs);
  QMetaObject::invokeMethod(mThreadContext, [this, shared = mShared, factory = mFactory, model, generation] {
    std::shared_ptr<pcm::prompter::PrompterEngine> engine;
    try { engine = factory(model); } catch (...) {}
    const bool loaded = engine != nullptr;
    if (shared->generation == generation) {
      std::lock_guard lock(shared->mutex);
      shared->engine = std::move(engine);
    }
    engine.reset(); // a superseded engine is released here, on the model thread
    QMetaObject::invokeMethod(this, [this, generation, loaded] {
      if (generation != mGeneration || mStopped) return;
      if (!loaded) { fail(); return; }
      mReady = true; mTimeout.stop(); emit ready();
      if (!mPending.isEmpty()) { const auto request = mPending; mPending = {}; send(request); }
    }, Qt::QueuedConnection);
  }, Qt::QueuedConnection);
  return true;
}

void PrompterWorker::stop() {
  mStopped = true; mReady = false; mTimeout.stop(); mPending = {}; mExpected = {};
  mShared->generation = ++mGeneration;
  mShared->cancel();
  QMetaObject::invokeMethod(mThreadContext, [shared = mShared] {
    std::shared_ptr<pcm::prompter::PrompterEngine> engine;
    {
      std::lock_guard lock(shared->mutex);
      engine = std::move(shared->engine);
    }
  }, Qt::QueuedConnection);
}

void PrompterWorker::fail() { stop(); emit failed(); }

void PrompterWorker::send(const QJsonObject &request) {
  if (mStopped) { emit failed(); return; }
  if (!mReady) {
    if (!mPending.isEmpty()) { fail(); return; }
    mPending = request; return;
  }
  if (QJsonDocument(request).toJson(QJsonDocument::Compact).size() > kMaxRequestBytes) { fail(); return; }
  mExpected = request;
  mTimeout.start(kRequestTimeoutMs);
  const int generation = mGeneration;
  QMetaObject::invokeMethod(mThreadContext, [this, shared = mShared, request, generation] {
    const auto engine = shared->current();
    if (!engine || shared->generation != generation) return;
    QJsonObject result;
    try {
      result = pcm::prompter::processRequest(request, [&engine](const QString &system, const QByteArray &payload) {
        return engine->generate(system, payload);
      });
    } catch (...) {
      // Never expose exception texts: they may describe source text or prompts.
      result = {{"protocol_version", 1}, {"error", "inference_failed"}};
      for (const char *key : {"request_id", "session_generation", "ledger_revision"})
        if (request.contains(key)) result.insert(key, request.value(key));
    }
    QMetaObject::invokeMethod(this, [this, generation, result] { finish(generation, result); }, Qt::QueuedConnection);
  }, Qt::QueuedConnection);
}

void PrompterWorker::finish(int generation, const QJsonObject &result) {
  if (generation != mGeneration || mStopped) return;
  if (mExpected.isEmpty() || result["request_id"] != mExpected["request_id"] ||
      result["session_generation"] != mExpected["session_generation"] ||
      result["ledger_revision"] != mExpected["ledger_revision"]) { fail(); return; }
  mExpected = {}; mTimeout.stop();
  emit resultReady(result);
}
