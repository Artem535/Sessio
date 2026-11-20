#include "transcription_consent_dialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <gtest/gtest.h>

TEST(TranscriptionConsentDialogTest, StartButtonDisabledUntilConsentBoxChecked) {
  TranscriptionConsentDialog dialog;
  EXPECT_FALSE(dialog.consentChecked());
  EXPECT_FALSE(dialog.startButton()->isEnabled());
  dialog.findChild<QCheckBox *>()->setChecked(true);
  EXPECT_TRUE(dialog.consentChecked());
  EXPECT_TRUE(dialog.startButton()->isEnabled());
  dialog.startButton()->click();
  EXPECT_EQ(dialog.result(), QDialog::Accepted);
}

TEST(TranscriptionConsentDialogTest, UncheckingDisablesStartAgain) {
  TranscriptionConsentDialog dialog;
  auto *box = dialog.findChild<QCheckBox *>();
  box->setChecked(true);
  box->setChecked(false);
  EXPECT_FALSE(dialog.consentChecked());
  EXPECT_FALSE(dialog.startButton()->isEnabled());
}

TEST(TranscriptionConsentDialogTest, AcceptingWithoutConsentIsNotPossible) {
  TranscriptionConsentDialog dialog;
  QSignalSpy accepted(&dialog, &QDialog::accepted);
  dialog.startButton()->click();
  dialog.accept();
  dialog.done(QDialog::Accepted);
  EXPECT_EQ(accepted.count(), 0);
  EXPECT_NE(dialog.result(), QDialog::Accepted);
}

TEST(TranscriptionConsentDialogTest, TextMentionsLocalProcessingNoAudioStorageRevocationAndEditing) {
  TranscriptionConsentDialog dialog;
  for (const char *name : {"consentLocalNote", "consentNoAudioNote", "consentRevokeNote", "consentEditNote"}) {
    auto *label = dialog.findChild<QLabel *>(name);
    ASSERT_NE(label, nullptr);
    EXPECT_FALSE(label->text().isEmpty());
  }
}

TEST(TranscriptionConsentDialogTest, CancelRejects) {
  TranscriptionConsentDialog dialog;
  QSignalSpy rejected(&dialog, &QDialog::rejected);
  dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
  EXPECT_EQ(rejected.count(), 1);
  EXPECT_EQ(dialog.result(), QDialog::Rejected);
}

int main(int argc, char **argv) {
  QTemporaryDir isolatedHome;
  qputenv("HOME", isolatedHome.path().toUtf8());
  qputenv("XDG_CONFIG_HOME", isolatedHome.path().toUtf8());
  QStandardPaths::setTestModeEnabled(true);
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
