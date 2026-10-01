#include "device_check_widget.h"
#include "device_manager.h"

#include <QApplication>
#include <QComboBox>
#include <QLayout>
#include <QLabel>
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

// Layout contract: the camera preview and the selector card sit side by side in one body row
// (preview first); the selectors live in the card with captions and a microphone level meter;
// Test sits next to the speaker selector; Back comes before Join.
TEST(DeviceCheckWidgetTest, PutsThePreviewBesideASelectorCardWithMeterAndSpeakerTest) {
  pcm::video::DeviceManager deviceManager;
  DeviceCheckWidget widget(&deviceManager);

  auto *body = widget.findChild<QWidget *>("deviceCheckBody");
  auto *preview = widget.findChild<QLabel *>("devicePreviewLabel");
  auto *card = widget.findChild<QWidget *>("deviceCard");
  ASSERT_NE(body, nullptr);
  ASSERT_NE(preview, nullptr);
  ASSERT_NE(card, nullptr);
  EXPECT_EQ(preview->parentWidget(), body);
  EXPECT_EQ(card->parentWidget(), body);
  EXPECT_LT(body->layout()->indexOf(preview), body->layout()->indexOf(card));

  for (const char *name : {"cameraCombo", "microphoneCombo", "speakerCombo"}) {
    auto *combo = widget.findChild<QComboBox *>(name);
    ASSERT_NE(combo, nullptr) << name;
    EXPECT_TRUE(card->isAncestorOf(combo)) << name;
  }
  EXPECT_TRUE(card->isAncestorOf(widget.findChild<QWidget *>("micLevelMeter")));
  auto *test = widget.findChild<QPushButton *>("testSpeakerButton");
  ASSERT_NE(test, nullptr);
  EXPECT_TRUE(card->isAncestorOf(test));

  auto *back = widget.findChild<QPushButton *>("backFromDeviceCheckButton");
  auto *join = widget.findChild<QPushButton *>("joinButton");
  ASSERT_NE(back, nullptr);
  ASSERT_NE(join, nullptr);
  EXPECT_EQ(back->parentWidget(), join->parentWidget());
  EXPECT_LT(back->parentWidget()->layout()->indexOf(back), back->parentWidget()->layout()->indexOf(join));
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
