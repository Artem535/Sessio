#include "device_check_widget.h"
#include "device_manager.h"

#include <QApplication>
#include <QComboBox>
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

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
