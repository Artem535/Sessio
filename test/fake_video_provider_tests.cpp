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
  EXPECT_EQ(provider.localVideoWidget(), nullptr);
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

TEST(FakeVideoProviderTest, ExposesTestSuppliedLocalVideoWidget) {
  FakeVideoProvider provider;
  QLabel widget;
  provider.mLocalVideoWidget = &widget;
  EXPECT_EQ(provider.localVideoWidget(), &widget);
}

int main(int argc, char **argv) {
  // QApplication is required here despite no widget ever being shown:
  // ExposesTestSuppliedLocalVideoWidget constructs a QLabel, and Qt aborts
  // ("Must construct a QApplication before a QWidget") on any QWidget
  // construction without one — matches call_page_tests.cpp's own main().
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
