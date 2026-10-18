#include "task_gate.h"
#include <gtest/gtest.h>
#include <future>
#include <semaphore>
#include <thread>

using sessio::litert::detail::TaskGate;

namespace {
class FinishedTask final : public TaskGate::Task {
public:
  bool wait() override { return true; }
  void cancel() override { ++cancellations; }
  int cancellations = 0;
};

class BlockingTask final : public TaskGate::Task {
public:
  bool wait() override { released.acquire(); return false; }
  void cancel() override { released.release(); }
  std::binary_semaphore released{0};
};
}

TEST(PrompterTaskGateTest, CancellationBeforeSubmissionPreventsPrefill) {
  TaskGate gate;
  ASSERT_TRUE(gate.beginGeneration());
  gate.cancel();
  int submitted = 0;
  EXPECT_EQ(gate.run([&] { ++submitted; return std::make_shared<FinishedTask>(); }), TaskGate::Result::Cancelled);
  EXPECT_EQ(submitted, 0);
}

TEST(PrompterTaskGateTest, CancellationBetweenStagesPreventsDecode) {
  TaskGate gate;
  ASSERT_TRUE(gate.beginGeneration());
  auto prefill = std::make_shared<FinishedTask>();
  ASSERT_EQ(gate.run([&] { return prefill; }), TaskGate::Result::Done);
  gate.cancel();
  int decoded = 0;
  EXPECT_EQ(gate.run([&] { ++decoded; return std::make_shared<FinishedTask>(); }), TaskGate::Result::Cancelled);
  EXPECT_EQ(decoded, 0);
  EXPECT_EQ(prefill->cancellations, 0);
}

TEST(PrompterTaskGateTest, CancellationRacingWithSubmissionReachesThePublishedTask) {
  TaskGate gate;
  ASSERT_TRUE(gate.beginGeneration());
  std::binary_semaphore submitting{0}, allowSubmit{0}, cancelling{0};
  auto task = std::make_shared<BlockingTask>();
  auto result = std::async(std::launch::async, [&] {
    return gate.run([&] {
      submitting.release();
      allowSubmit.acquire();
      return task;
    });
  });
  submitting.acquire();
  std::thread cancel([&] { cancelling.release(); gate.cancel(); });
  cancelling.acquire();
  allowSubmit.release();
  cancel.join();
  EXPECT_EQ(result.get(), TaskGate::Result::Cancelled);
}

TEST(PrompterTaskGateTest, FailedSubmissionDoesNotPublishATask) {
  TaskGate gate;
  ASSERT_TRUE(gate.beginGeneration());
  EXPECT_EQ(gate.run([] { return std::shared_ptr<TaskGate::Task>(); }), TaskGate::Result::Failed);
  gate.cancel();
  EXPECT_FALSE(gate.beginGeneration());
  EXPECT_TRUE(gate.beginGeneration());
}
