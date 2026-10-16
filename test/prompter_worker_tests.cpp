#include "prompter_worker.h"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <gtest/gtest.h>

namespace {

struct Probe {
  std::atomic_int loads = 0, alive = 0, cancels = 0;
  std::atomic_bool block = false;
  QThread *loadThread = nullptr;
};

class FakeEngine final : public pcm::prompter::PrompterEngine {
public:
  explicit FakeEngine(Probe &probe) : mProbe(probe) { ++mProbe.alive; }
  ~FakeEngine() override { --mProbe.alive; }
  QJsonValue generate(const QString &, const QByteArray &) override {
    while (mProbe.block && !mCancelled) QThread::msleep(5);
    if (mCancelled) throw std::runtime_error("cancelled");
    return QJsonObject{{"events", QJsonArray{}}};
  }
  void cancel() override { ++mProbe.cancels; mCancelled = true; }
private:
  Probe &mProbe;
  std::atomic_bool mCancelled = false;
};

pcm::prompter::PrompterEngineFactory factory(Probe &probe) {
  return [&probe](const QString &model) -> std::unique_ptr<pcm::prompter::PrompterEngine> {
    ++probe.loads;
    probe.loadThread = QThread::currentThread();
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
  Probe probe;
};

} // namespace

TEST_F(PrompterWorkerTest, MissingModelFailsWithoutLoading) {
  PrompterWorker worker(factory(probe));
  QSignalSpy failed(&worker, &PrompterWorker::failed);
  EXPECT_FALSE(worker.start(dir.filePath("absent.litertlm")));
  EXPECT_EQ(failed.count(), 1);
  EXPECT_EQ(probe.loads, 0);
}

TEST_F(PrompterWorkerTest, ModelLoadsAndRunsOffTheGuiThread) {
  PrompterWorker worker(factory(probe));
  QSignalSpy ready(&worker, &PrompterWorker::ready);
  QSignalSpy replies(&worker, &PrompterWorker::resultReady);
  ASSERT_TRUE(worker.start(model));
  worker.send(request(5)); // queued until the model is loaded
  ASSERT_TRUE(replies.wait(5000));
  EXPECT_EQ(ready.count(), 1);
  EXPECT_NE(probe.loadThread, QThread::currentThread());
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
  EXPECT_EQ(reply.size(), 5);
}

TEST_F(PrompterWorkerTest, StopCancelsInferenceDropsItsResultAndReleasesTheModel) {
  PrompterWorker worker(factory(probe));
  QSignalSpy ready(&worker, &PrompterWorker::ready);
  QSignalSpy replies(&worker, &PrompterWorker::resultReady);
  ASSERT_TRUE(worker.start(model));
  ASSERT_TRUE(ready.wait(5000));
  probe.block = true;
  worker.send(request(1));
  QTest::qWait(50);
  worker.stop();
  for (int i = 0; i < 500 && probe.alive > 0; ++i) QTest::qWait(10);
  EXPECT_EQ(probe.alive, 0);
  EXPECT_GE(probe.cancels, 1);
  QTest::qWait(50);
  EXPECT_EQ(replies.count(), 0);
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
  EXPECT_EQ(probe.alive, 1);
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
