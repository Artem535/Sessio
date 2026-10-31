#include "audio_capture_adapter.h"

#include <QBuffer>
#include <QCoreApplication>
#include <gtest/gtest.h>
#include <livekit/livekit.h>
#include <thread>

namespace {
class FakeSource : public pcm::video::AudioCaptureSource {
public:
  QBuffer input;
  bool fails = false;
  QIODevice *start() override {
    input.setData(QByteArray(480 * 2 * 20, '\0'));
    input.open(QIODevice::ReadOnly);
    return fails ? nullptr : &input;
  }
  void stop() override { emit input.readyRead(); }
};
}

TEST(AudioCaptureLifecycle, StopDuringFrameDeliveryDiscardsOldBatch) {
  FakeSource *source = nullptr;
  pcm::video::AudioCaptureAdapter adapter([&](const QAudioDevice &) {
    auto result = std::make_unique<FakeSource>();
    source = result.get();
    return result;
  });
  ASSERT_TRUE(adapter.start({}));
  int frames = 0;
  QObject::connect(&adapter, &pcm::video::AudioCaptureAdapter::frameCaptured, [&] {
    ++frames;
    if (frames == 1) adapter.stop();
  });
  emit source->input.readyRead();
  EXPECT_EQ(frames, 1);
}

TEST(AudioCaptureLifecycle, FailedSelectionRestoresPreviousSourceOnce) {
  int attempts = 0;
  pcm::video::AudioCaptureAdapter adapter([&](const QAudioDevice &) {
    auto result = std::make_unique<FakeSource>();
    result->fails = ++attempts == 2;
    return result;
  });
  ASSERT_TRUE(adapter.start({}));
  const auto published = adapter.audioSource();
  adapter.tap().setEnabled(false);
  ASSERT_TRUE(adapter.start({}));
  EXPECT_EQ(attempts, 3);
  EXPECT_EQ(adapter.audioSource(), published);
  EXPECT_TRUE(adapter.activeDevice().has_value());
  EXPECT_FALSE(adapter.tap().active());
}

TEST(AudioCaptureLifecycle, FailedRecoveryDisablesCaptureWithoutRepeatedRetries) {
  int attempts = 0;
  pcm::video::AudioCaptureAdapter adapter([&](const QAudioDevice &) {
    auto result = std::make_unique<FakeSource>();
    result->fails = ++attempts > 1;
    return result;
  });
  ASSERT_TRUE(adapter.start({}));
  ASSERT_FALSE(adapter.start({}));
  EXPECT_EQ(attempts, 3);
  EXPECT_FALSE(adapter.activeDevice().has_value());
}

TEST(AudioCaptureLifecycle, QueuedOldSourceCallbackDoesNotReadReplacement) {
  FakeSource *source = nullptr;
  pcm::video::AudioCaptureAdapter adapter([&](const QAudioDevice &) {
    auto result = std::make_unique<FakeSource>();
    source = result.get();
    return result;
  });
  ASSERT_TRUE(adapter.start({}));
  std::thread producer([&] { emit source->input.readyRead(); });
  producer.join();
  ASSERT_TRUE(adapter.start({}));
  QCoreApplication::processEvents();
  EXPECT_EQ(adapter.framesCaptured(), 0);
  emit source->input.readyRead();
  EXPECT_EQ(adapter.framesCaptured(), 20);
}

TEST(AudioCaptureLifecycle, DestructionDuringFrameDeliveryEndsBatchSafely) {
  FakeSource *source = nullptr;
  auto adapter = std::make_unique<pcm::video::AudioCaptureAdapter>([&](const QAudioDevice &) {
    auto result = std::make_unique<FakeSource>();
    source = result.get();
    return result;
  });
  ASSERT_TRUE(adapter->start({}));
  QObject::connect(adapter.get(), &pcm::video::AudioCaptureAdapter::frameCaptured,
                   [&] { adapter.reset(); });
  emit source->input.readyRead();
  EXPECT_FALSE(adapter);
}

TEST(AudioCaptureLifecycle, StopDuringResumeCancelsRecovery) {
  int attempts = 0;
  pcm::video::AudioCaptureAdapter adapter([&](const QAudioDevice &) {
    ++attempts;
    return std::make_unique<FakeSource>();
  });
  QObject::connect(&adapter, &pcm::video::AudioCaptureAdapter::captureResumed,
                   [&] { adapter.stop(); });
  EXPECT_FALSE(adapter.start({}));
  EXPECT_EQ(attempts, 1);
  EXPECT_FALSE(adapter.activeDevice());
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  livekit::initialize(livekit::LogLevel::Warn);
  const int result = RUN_ALL_TESTS();
  livekit::shutdown();
  return result;
}
