#include "transcription_settings_panel.h"
#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <gtest/gtest.h>

TEST(TranscriptionSettingsPanelTest, ProgrammaticStateDoesNotEmitAndToggleDoes) {
  TranscriptionSettingsPanel panel;
  QSignalSpy spy(&panel, &TranscriptionSettingsPanel::enabledToggled);
  panel.setEnabledState(true);
  EXPECT_EQ(spy.count(), 0);
  auto *toggle = panel.findChild<QCheckBox *>("transcriptionEnabled");
  ASSERT_NE(toggle, nullptr);
  EXPECT_TRUE(toggle->isChecked());
  toggle->click();
  ASSERT_EQ(spy.count(), 1);
  EXPECT_FALSE(spy.front().front().toBool());
}
TEST(TranscriptionSettingsPanelTest, DeletionRequiresDataAndNoActiveSession) {
  TranscriptionSettingsPanel panel;
  auto *button = panel.findChild<QPushButton *>("deleteAllTranscripts");
  ASSERT_NE(button, nullptr);
  panel.setTranscriptCount(0);
  EXPECT_FALSE(button->isEnabled());
  panel.setTranscriptCount(2);
  EXPECT_TRUE(button->isEnabled());
  panel.setDeleteAllAllowed(false, "Recording");
  EXPECT_FALSE(button->isEnabled());
  EXPECT_EQ(button->toolTip(), "Recording");
  panel.setTranscriptCount(3);
  EXPECT_FALSE(button->isEnabled());
  panel.setDeleteAllAllowed(true, {});
  QSignalSpy spy(&panel, &TranscriptionSettingsPanel::deleteAllRequested);
  button->click();
  EXPECT_EQ(spy.count(), 1);
}
TEST(TranscriptionSettingsPanelTest, ModelInfoReflectsAvailability) {
  TranscriptionSettingsPanel panel;
  panel.setModelInfo("gigaam-v3-rnnt", false);
  auto *label = panel.findChild<QLabel *>("transcriptionModelInfo");
  ASSERT_NE(label, nullptr);
  EXPECT_TRUE(label->text().contains("not installed"));
  panel.setModelInfo("gigaam-v3-rnnt", true);
  EXPECT_TRUE(label->text().contains("gigaam-v3-rnnt"));
  EXPECT_FALSE(label->text().contains("not installed"));
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
