#include "device_manager.h"

#include <QCoreApplication>
#include <QMediaDevices>
#include <QSignalSpy>
#include <gtest/gtest.h>

TEST(DeviceManagerTest, ListsCamerasMicrophonesAndSpeakersWithoutCrashing) {
  int argc = 0;
  QCoreApplication app(argc, nullptr);

  pcm::video::DeviceManager manager;

  // No real devices are guaranteed in a CI/sandboxed environment — this test
  // proves DeviceManager doesn't crash and returns well-formed (possibly
  // empty) lists, not that specific hardware is present.
  EXPECT_NO_THROW(static_cast<void>(manager.cameras()));
  EXPECT_NO_THROW(static_cast<void>(manager.microphones()));
  EXPECT_NO_THROW(static_cast<void>(manager.speakers()));
}

TEST(DeviceManagerTest, DefaultCameraIsNulloptWhenNoCamerasPresent) {
  int argc = 0;
  QCoreApplication app(argc, nullptr);

  pcm::video::DeviceManager manager;
  if (manager.cameras().isEmpty()) {
    EXPECT_FALSE(manager.defaultCamera().has_value());
  } else {
    EXPECT_TRUE(manager.defaultCamera().has_value());
  }
}

// Fixwave userfeedback group A, bug 3: camera/microphone/speaker lists never
// refreshed after a device was plugged in or enabled while the app was
// running -- DeviceManager was a plain, non-observable wrapper, so nothing
// ever told a UI to re-query it. DeviceManager is now QObject-derived and
// relays QMediaDevices' own change signals as devicesChanged().
//
// A real hot-plug event can't be produced in this sandboxed environment (no
// guaranteed real or virtual camera/audio backend), so this drives the exact
// mechanism a real event would use instead: `signals:` compiles down to
// `public:`, so calling a Qt signal directly (the `emit` keyword itself is a
// no-op macro) is a legitimate, if unusual, way to invoke it deterministically.
// The test-only constructor overload hands DeviceManager a QMediaDevices
// instance this test owns and can call signals on directly, proving the
// videoInputsChanged()/audioInputsChanged()/audioOutputsChanged() ->
// devicesChanged() wiring set up in DeviceManager's constructor actually
// fires -- the same wiring the production (default) constructor sets up
// against its own internally owned QMediaDevices.
TEST(DeviceManagerTest, DevicesChangedFiresWhenUnderlyingMediaDeviceSignalsFire) {
  int argc = 0;
  QCoreApplication app(argc, nullptr);

  QMediaDevices mediaDevices;
  pcm::video::DeviceManager manager(&mediaDevices);
  QSignalSpy spy(&manager, &pcm::video::DeviceManager::devicesChanged);

  emit mediaDevices.videoInputsChanged();
  EXPECT_EQ(spy.count(), 1);

  emit mediaDevices.audioInputsChanged();
  EXPECT_EQ(spy.count(), 2);

  emit mediaDevices.audioOutputsChanged();
  EXPECT_EQ(spy.count(), 3);
}

// A call must open the device the operating system treats as the default,
// not whichever device happens to be listed first: with a headset connected
// the first listed output is often the built-in analog one, so the user heard
// the call on the wrong device. Hardware-dependent like the tests above, so it
// only asserts when the platform reports a default at all.
TEST(DeviceManagerTest, DefaultDevicesFollowTheSystemDefaultsNotListOrder) {
  int argc = 0;
  QCoreApplication app(argc, nullptr);

  pcm::video::DeviceManager manager;

  if (const auto systemOutput = QMediaDevices::defaultAudioOutput(); !systemOutput.isNull()) {
    ASSERT_TRUE(manager.defaultSpeaker().has_value());
    EXPECT_EQ(manager.defaultSpeaker()->id(), systemOutput.id());
  }
  if (const auto systemInput = QMediaDevices::defaultAudioInput(); !systemInput.isNull()) {
    ASSERT_TRUE(manager.defaultMicrophone().has_value());
    EXPECT_EQ(manager.defaultMicrophone()->id(), systemInput.id());
  }
  if (const auto systemCamera = QMediaDevices::defaultVideoInput(); !systemCamera.isNull()) {
    ASSERT_TRUE(manager.defaultCamera().has_value());
    EXPECT_EQ(manager.defaultCamera()->id(), systemCamera.id());
  }
}
