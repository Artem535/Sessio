#include "device_manager.h"

#include <QCoreApplication>
#include <gtest/gtest.h>

TEST(DeviceManagerTest, ListsCamerasMicrophonesAndSpeakersWithoutCrashing) {
  int argc = 0;
  QCoreApplication app(argc, nullptr);

  pcm::video::DeviceManager manager;

  // No real devices are guaranteed in a CI/sandboxed environment — this test
  // proves DeviceManager doesn't crash and returns well-formed (possibly
  // empty) lists, not that specific hardware is present.
  EXPECT_NO_THROW(manager.cameras());
  EXPECT_NO_THROW(manager.microphones());
  EXPECT_NO_THROW(manager.speakers());
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
