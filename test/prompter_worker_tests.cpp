#include "prompter_worker.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QPointer>
#include <QSemaphore>
#include <QTest>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <thread>
#include <gtest/gtest.h>

namespace {

struct Probe {
  std::atomic_int loads = 0, alive = 0, cancels = 0;
  std::atomic_int destroyed = 0;
  std::atomic_bool block = false, ignoreCancel = false;
  std::atomic<QThread *> loadThread = nullptr, destroyThread = nullptr;
};

class FakeEngine final : public pcm::prompter::PrompterEngine {
public:
  explicit FakeEngine(std::shared_ptr<Probe> probe) : mProbe(std::move(probe)) { ++mProbe->alive; }
  ~FakeEngine() override {
    mProbe->destroyThread = QThread::currentThread();
    --mProbe->alive;
    ++mProbe->destroyed;
  }
  QJsonValue generate(const QString &, const QByteArray &, const QByteArray &) override {
    while (mProbe->block && (!mCancelled || mProbe->ignoreCancel)) QThread::msleep(5);
    if (mCancelled.exchange(false)) throw std::runtime_error("cancelled");
    return QJsonObject{{"events", QJsonArray{}}};
  }
  void cancel() override { ++mProbe->cancels; mCancelled = true; }
private:
  std::shared_ptr<Probe> mProbe;
  std::atomic_bool mCancelled = false;
};

pcm::prompter::PrompterEngineFactory factory(const std::shared_ptr<Probe> &probe) {
  return [probe](const QString &model) -> std::unique_ptr<pcm::prompter::PrompterEngine> {
    ++probe->loads;
    probe->loadThread = QThread::currentThread();
    if (model.endsWith("broken")) return nullptr;
    return std::make_unique<FakeEngine>(probe);
  };
}

QJsonObject request(int id) {
  return {{"protocol_version", 1}, {"request_id", id}, {"session_generation", 1}, {"ledger_revision", 0},
          {"utterances", QJsonArray{QJsonObject{{"id", "1"}, {"speaker", "A"}, {"text", "Привет"}}}},
          {"candidates", QJsonArray{}}};
}

class PrompterWorkerTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(dir.isValid());
    model = dir.filePath("model.litertlm");
    QFile file(model); ASSERT_TRUE(file.open(QIODevice::WriteOnly)); file.write("x");
  }
  QTemporaryDir dir;
  QString model;
  std::shared_ptr<Probe> probe = std::make_shared<Probe>();
};

} // namespace

TEST_F(PrompterWorkerTest, CancellingColdNotesDrainsTheOtherQueuedRequest) {
  auto entered = std::make_shared<QSemaphore>();
  auto gate = std::make_shared<QSemaphore>();
  PrompterWorker worker([probe = probe, entered, gate](const QString &) -> std::unique_ptr<pcm::prompter::PrompterEngine> {
    entered->release(); gate->acquire(); return std::make_unique<FakeEngine>(probe);
  });
  QSignalSpy replies(&worker, &PrompterWorker::resultReady);
  ASSERT_TRUE(worker.start(model));
  ASSERT_TRUE(entered->tryAcquire(1, 5000));
  auto one = request(1); one["task"] = "notes"; one["material_id"] = 1; one["request_token"] = "one";
  worker.send(one);
  auto two = request(2); two["task"] = "notes"; two["material_id"] = 2; two["request_token"] = "two";
  worker.send(two); worker.cancelNotes(1, "one"); gate->release();
  ASSERT_TRUE(replies.wait(5000)); ASSERT_EQ(replies.count(), 1);
  EXPECT_EQ(replies[0][0].toJsonObject()["request_id"].toInt(), 2);
}

TEST_F(PrompterWorkerTest, MissingModelFailsWithoutLoading) {
  PrompterWorker worker(factory(probe));
  QSignalSpy failed(&worker, &PrompterWorker::failed);
  EXPECT_FALSE(worker.start(dir.filePath("absent.litertlm")));
  EXPECT_EQ(failed.count(), 1);
  EXPECT_EQ(probe->loads, 0);
}

TEST_F(PrompterWorkerTest, ModelLoadsAndRunsOffTheGuiThread) {
  PrompterWorker worker(factory(probe));
  QSignalSpy ready(&worker, &PrompterWorker::ready);
  QSignalSpy replies(&worker, &PrompterWorker::resultReady);
  ASSERT_TRUE(worker.start(model));
  worker.send(request(5)); // queued until the model is loaded
  ASSERT_TRUE(replies.wait(5000));
  EXPECT_EQ(ready.count(), 1);
  EXPECT_NE(probe->loadThread.load(), QThread::currentThread());
  const auto reply = replies.first().first().toJsonObject();
  EXPECT_EQ(reply["request_id"].toInt(), 5);
  EXPECT_TRUE(reply["events"].toArray().isEmpty());
}

TEST_F(PrompterWorkerTest, FailedLoadIsReported) {
  QFile(model).rename(model + ".broken");
  PrompterWorker worker(factory(probe));
  QSignalSpy failed(&worker, &PrompterWorker::failed);
  ASSERT_TRUE(worker.start(model + ".broken"));
  ASSERT_TRUE(failed.wait(5000));
}

TEST_F(PrompterWorkerTest, InferenceErrorsReachTheControllerWithoutDetails) {
  PrompterWorker worker(factory(probe));
  QSignalSpy replies(&worker, &PrompterWorker::resultReady);
  ASSERT_TRUE(worker.start(model));
  auto broken = request(9);
  broken["utterances"] = QJsonArray{};
  worker.send(broken);
  ASSERT_TRUE(replies.wait(5000));
  const auto reply = replies.first().first().toJsonObject();
  EXPECT_EQ(reply["error"].toString(), "inference_failed");
  EXPECT_EQ(reply["request_id"].toInt(), 9);
  // Only a fixed category, never the exception text.
  EXPECT_EQ(reply["reason"].toString(), "runtime");
  EXPECT_EQ(reply.size(), 6);
}

TEST_F(PrompterWorkerTest, StopCancelsInferenceDropsItsResultAndReleasesTheModel) {
  PrompterWorker worker(factory(probe));
  QSignalSpy ready(&worker, &PrompterWorker::ready);
  QSignalSpy replies(&worker, &PrompterWorker::resultReady);
  ASSERT_TRUE(worker.start(model));
  ASSERT_TRUE(ready.wait(5000));
  probe->block = true;
  worker.send(request(1));
  QTest::qWait(50);
  worker.stop();
  for (int i = 0; i < 500 && probe->alive > 0; ++i) QTest::qWait(10);
  EXPECT_EQ(probe->alive, 0);
  EXPECT_GE(probe->cancels, 1);
  QTest::qWait(50);
  EXPECT_EQ(replies.count(), 0);
}

TEST_F(PrompterWorkerTest, SlowCallTimesOutWithoutReleasingTheModel) {
  PrompterWorker worker(factory(probe));
  worker.setTimeouts(100, 5000);
  QSignalSpy ready(&worker, &PrompterWorker::ready);
  QSignalSpy replies(&worker, &PrompterWorker::resultReady);
  QSignalSpy failed(&worker, &PrompterWorker::failed);
  ASSERT_TRUE(worker.start(model));
  ASSERT_TRUE(ready.wait(5000));
  probe->block = true;
  worker.send(request(1));
  ASSERT_TRUE(replies.wait(5000));
  const auto reply = replies[0][0].toJsonObject();
  EXPECT_EQ(reply["request_id"].toInt(), 1);
  EXPECT_EQ(reply["error"].toString(), "inference_failed");
  EXPECT_EQ(reply["reason"].toString(), "timeout");
  EXPECT_EQ(failed.count(), 0);
  EXPECT_EQ(probe->loads, 1);
  EXPECT_EQ(probe->alive, 1);
  probe->block = false;
  worker.send(request(2));
  ASSERT_TRUE(replies.wait(5000));
  EXPECT_FALSE(replies[1][0].toJsonObject().contains("error"));
}

TEST_F(PrompterWorkerTest, CallIgnoringCancellationReleasesTheModel) {
  PrompterWorker worker(factory(probe));
  worker.setTimeouts(100, 100);
  QSignalSpy ready(&worker, &PrompterWorker::ready);
  QSignalSpy failed(&worker, &PrompterWorker::failed);
  ASSERT_TRUE(worker.start(model));
  ASSERT_TRUE(ready.wait(5000));
  probe->block = true; probe->ignoreCancel = true;
  worker.send(request(1));
  ASSERT_TRUE(failed.wait(5000));
  probe->block = false;
  for (int i = 0; i < 500 && probe->alive > 0; ++i) QTest::qWait(10);
  EXPECT_EQ(probe->alive, 0);
}

TEST_F(PrompterWorkerTest, StopAndImmediateRestartLoadsAFreshModel) {
  PrompterWorker worker(factory(probe));
  QSignalSpy ready(&worker, &PrompterWorker::ready);
  ASSERT_TRUE(worker.start(model));
  worker.stop();
  ASSERT_TRUE(worker.start(model));
  ASSERT_TRUE(ready.wait(5000));
  QTest::qWait(50);
  EXPECT_EQ(ready.count(), 1);
  EXPECT_EQ(probe->alive, 1);
}

TEST_F(PrompterWorkerTest, DestructionReturnsWhileFactoryIsBlockedAndLaterCleansUpOnModelThread) {
  struct Gate {
    QSemaphore entered, release, destructorReturned;
    std::atomic_bool returned = false;
  };
  const auto gate = std::make_shared<Gate>();
  auto worker = std::make_unique<PrompterWorker>([probe = probe, gate](const QString &) {
    probe->loadThread = QThread::currentThread();
    gate->entered.release();
    gate->release.acquire();
    auto engine = std::make_unique<FakeEngine>(probe);
    gate->returned = true;
    return engine;
  });
  QSignalSpy ready(worker.get(), &PrompterWorker::ready);
  QSignalSpy replies(worker.get(), &PrompterWorker::resultReady);
  QSignalSpy failed(worker.get(), &PrompterWorker::failed);
  ASSERT_TRUE(worker->start(model));
  worker->send(request(1));
  const bool entered = gate->entered.tryAcquire(1, 5000);
  if (!entered) gate->release.release();
  ASSERT_TRUE(entered);
  auto *modelThread = probe->loadThread.load();
  QPointer<QThread> thread(modelThread);
  QSignalSpy finished(modelThread, &QThread::finished);

  // Bound a failure of the old joining destructor too: only the watchdog
  // releases the factory if destruction incorrectly waits for it.
  std::thread watchdog([gate] {
    if (!gate->destructorReturned.tryAcquire(1, 1000)) gate->release.release();
  });
  QElapsedTimer timer;
  timer.start();
  worker.reset();
  const auto elapsed = timer.elapsed();
  gate->destructorReturned.release();
  watchdog.join();
  EXPECT_LT(elapsed, 500);
  EXPECT_FALSE(gate->returned.load());
  EXPECT_EQ(finished.count(), 0);

  gate->release.release();
  for (int i = 0; i < 500 && finished.isEmpty(); ++i) QTest::qWait(10);
  EXPECT_EQ(finished.count(), 1);
  for (int i = 0; i < 500 && thread; ++i) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::qWait(10);
  }
  EXPECT_TRUE(thread.isNull());
  EXPECT_TRUE(gate->returned.load());
  EXPECT_EQ(probe->destroyed, 1);
  EXPECT_EQ(probe->alive, 0);
  EXPECT_EQ(probe->destroyThread.load(), modelThread);
  EXPECT_NE(probe->destroyThread.load(), QThread::currentThread());
  EXPECT_EQ(ready.count(), 0);
  EXPECT_EQ(replies.count(), 0);
  EXPECT_EQ(failed.count(), 0);
}

TEST_F(PrompterWorkerTest, LoadedModelIsReleasedWithoutPumpingTheGuiEventLoop) {
  auto worker = std::make_unique<PrompterWorker>(factory(probe));
  QSignalSpy ready(worker.get(), &PrompterWorker::ready);
  ASSERT_TRUE(worker->start(model));
  ASSERT_TRUE(ready.wait(5000));
  auto *modelThread = probe->loadThread.load();
  QPointer<QThread> thread(modelThread);
  const auto exited = std::make_shared<QSemaphore>();
  QObject::connect(modelThread, &QThread::finished, modelThread, [exited] {
    exited->release();
  }, Qt::DirectConnection);

  worker.reset();
  // No GUI event processing here: finished must release the engine even if
  // quit() skips the queued release and GUI deferred deletion cannot run yet.
  EXPECT_TRUE(exited->tryAcquire(1, 5000));
  EXPECT_EQ(probe->destroyed, 1);
  EXPECT_EQ(probe->alive, 0);
  EXPECT_EQ(probe->destroyThread.load(), modelThread);
  EXPECT_NE(probe->destroyThread.load(), QThread::currentThread());
  EXPECT_FALSE(thread.isNull());

  for (int i = 0; i < 500 && thread; ++i) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::qWait(10);
  }
  EXPECT_TRUE(thread.isNull());
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
