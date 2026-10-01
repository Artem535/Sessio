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
