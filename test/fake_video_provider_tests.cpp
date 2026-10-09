#include "fake_video_provider.h"

#include <QApplication>
#include <QCameraDevice>
#include <QAudioDevice>
#include <QLabel>
#include <gtest/gtest.h>

using pcm::video::test::FakeVideoProvider;

TEST(FakeVideoProviderTest, DefaultsToMicrophoneAndCameraEnabled) {
  FakeVideoProvider provider;
  EXPECT_TRUE(provider.isMicrophoneEnabled());
  EXPECT_TRUE(provider.isCameraEnabled());
  EXPECT_EQ(provider.frameSource("missing"), nullptr);
}

TEST(FakeVideoProviderTest, TracksMicrophoneAndCameraToggleCalls) {
  FakeVideoProvider provider;
  provider.setMicrophoneEnabled(false);
  EXPECT_FALSE(provider.isMicrophoneEnabled());
  EXPECT_EQ(provider.mSetMicrophoneEnabledCallCount, 1);

  provider.setCameraEnabled(false);
  EXPECT_FALSE(provider.isCameraEnabled());
  EXPECT_EQ(provider.mSetCameraEnabledCallCount, 1);
}

TEST(FakeVideoProviderTest, TracksDeviceSwitchCalls) {
  FakeVideoProvider provider;
  const QCameraDevice camera;
  const QAudioDevice microphone;
  const QAudioDevice speaker;

  provider.switchCamera(camera);
  provider.switchMicrophone(microphone);
  provider.switchSpeaker(speaker);

  EXPECT_EQ(provider.mSwitchCameraCallCount, 1);
  EXPECT_EQ(provider.mSwitchMicrophoneCallCount, 1);
  EXPECT_EQ(provider.mSwitchSpeakerCallCount, 1);
}

TEST(FakeVideoProviderTest, ExposesStableIndependentParticipantSources) {
  FakeVideoProvider provider;
  provider.simulateParticipantJoined({"local", "Me", "", true});
  provider.simulateParticipantJoined({"remote"});
  auto *first = provider.frameSource("remote");
  ASSERT_NE(first, nullptr);
  EXPECT_NE(first, provider.frameSource("local"));
  provider.simulateParticipantJoined({"remote", "Updated"});
  EXPECT_EQ(first, provider.frameSource("remote"));
  QPointer<pcm::video::VideoFrameSource> source(first);
  provider.simulateParticipantLeft("remote");
  EXPECT_TRUE(source.isNull());
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

namespace {
class RecordingSink final : public pcm::video::AudioSink {
public:
  void onAudio(const QString &id, const int16_t *samples, std::size_t count, int rate) override {
    ++calls;
    lastId = id;
    lastSamples.assign(samples, samples + count);
    lastRate = rate;
  }
  int calls{0};
  QString lastId;
  std::vector<int16_t> lastSamples;
  int lastRate{0};
};
} // namespace

TEST(FakeVideoProviderTest, FakeProviderForwardsSimulatedAudioToSink) {
  FakeVideoProvider provider;
  auto sink = std::make_shared<RecordingSink>();
  provider.setAudioSink(sink);
  provider.simulateAudio("p1", {1, 2, 3}, 48000);
  EXPECT_EQ(sink->calls, 1);
  EXPECT_EQ(sink->lastId, "p1");
  EXPECT_EQ(sink->lastSamples, (std::vector<int16_t>{1, 2, 3}));
  EXPECT_EQ(sink->lastRate, 48000);
}

TEST(FakeVideoProviderTest, FakeProviderWithoutSinkDropsAudio) {
  FakeVideoProvider provider;
  provider.simulateAudio("p1", {1, 2, 3}, 48000);
  EXPECT_EQ(provider.audioSink(), nullptr);
}
