#include "device_check_widget.h"
#include "device_manager.h"

#include <QApplication>
#include <QComboBox>
#include <QMediaDevices>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <gtest/gtest.h>

TEST(DeviceCheckWidgetTest, HasCameraMicrophoneSpeakerSelectors) {
  pcm::video::DeviceManager deviceManager;
  DeviceCheckWidget widget(&deviceManager);

  EXPECT_NE(widget.findChild<QComboBox *>("cameraCombo"), nullptr);
  EXPECT_NE(widget.findChild<QComboBox *>("microphoneCombo"), nullptr);
  EXPECT_NE(widget.findChild<QComboBox *>("speakerCombo"), nullptr);
}

TEST(DeviceCheckWidgetTest, ClickingJoinEmitsJoinRequested) {
  pcm::video::DeviceManager deviceManager;
  DeviceCheckWidget widget(&deviceManager);
  auto *joinButton = widget.findChild<QPushButton *>("joinButton");
  ASSERT_NE(joinButton, nullptr);

  QSignalSpy joinSpy(&widget, &DeviceCheckWidget::joinRequested);
  QTest::mouseClick(joinButton, Qt::LeftButton);

  EXPECT_EQ(joinSpy.count(), 1);
}

// The screen must start on what the OS treats as the default device, not on the
// first listed one (often a built-in analog output while a headset is connected).
TEST(DeviceCheckWidgetTest, PreselectsTheSystemDefaultDevices) {
  pcm::video::DeviceManager deviceManager;
  DeviceCheckWidget widget(&deviceManager);

  if (const auto output = QMediaDevices::defaultAudioOutput(); !output.isNull()) {
    EXPECT_EQ(widget.findChild<QComboBox *>("speakerCombo")->currentData().toByteArray(), output.id());
  }
  if (const auto input = QMediaDevices::defaultAudioInput(); !input.isNull()) {
    EXPECT_EQ(widget.findChild<QComboBox *>("microphoneCombo")->currentData().toByteArray(), input.id());
  }
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
