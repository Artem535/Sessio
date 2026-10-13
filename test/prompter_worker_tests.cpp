#include "prompter_worker.h"
#include <QApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <gtest/gtest.h>

TEST(PrompterWorkerTest, MissingRuntimeFailsWithoutStartingAProcess) {
  PrompterWorker worker;
  QSignalSpy failed(&worker, &PrompterWorker::failed);
  EXPECT_FALSE(worker.start("/does/not/exist", "", "", ""));
  EXPECT_EQ(failed.count(), 1);
}
TEST(PrompterWorkerTest, OnlyProtocolRepliesReachTheController) {
  PrompterWorker worker;
  QSignalSpy failed(&worker, &PrompterWorker::failed);
  QSignalSpy replies(&worker, &PrompterWorker::resultReady);
  worker.consumeOutput("{\"protocol_version\":1,\"ready\":true}\n");
  worker.consumeOutput("not JSON\n");
  EXPECT_EQ(replies.count(), 0);
  EXPECT_EQ(failed.count(), 1);
}
TEST(PrompterWorkerTest, OversizeUnterminatedOutputIsRejected) {
  PrompterWorker worker;
  QSignalSpy failed(&worker, &PrompterWorker::failed);
  worker.consumeOutput(QByteArray(256 * 1024 + 1, 'x'));
  EXPECT_EQ(failed.count(), 1);
}
TEST(PrompterWorkerTest, StopAndImmediateRestartStartsANewProcess) {
  QTemporaryDir dir;
  const auto path = dir.filePath("worker.sh");
  QFile script(path); ASSERT_TRUE(script.open(QIODevice::WriteOnly));
  script.write("printf '{\"protocol_version\":1,\"ready\":true}\\n'\nwhile read line; do sleep 1; done\n"); script.close();
  PrompterWorker worker;
  QSignalSpy ready(&worker, &PrompterWorker::ready);
  ASSERT_TRUE(worker.start("/bin/sh", path, path, path));
  ASSERT_TRUE(ready.wait(2000));
  worker.stop();
  ASSERT_TRUE(worker.start("/bin/sh", path, path, path));
  ASSERT_TRUE(ready.wait(2000));
  EXPECT_EQ(ready.count(), 2);
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
