#include "settings_dialog.h"
#include "fake_token_backend_credential_store.h"

#include <QApplication>
#include <QGroupBox>
#include <QGuiApplication>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QWidget>
#include <gtest/gtest.h>

namespace {
int pageIndexOf(SettingsDialog &dialog, const QString &objectName) {
  auto *stack = dialog.findChild<QStackedWidget *>();
  auto *control = dialog.findChild<QWidget *>(objectName);
  if (stack == nullptr || control == nullptr) {
    return -1;
  }
  QWidget *page = control;
  while (page != nullptr && stack->indexOf(page) == -1) {
    page = page->parentWidget();
  }
  return page == nullptr ? -1 : stack->indexOf(page);
}
} // namespace

TEST(SettingsDialogLayoutTest, CreateBackupButtonIsOnTheBackupPage) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  EXPECT_EQ(pageIndexOf(dialog, "createBackupButton"), 2);
}

TEST(SettingsDialogLayoutTest, BackupEncryptionSwitchIsOnTheBackupPage) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  EXPECT_EQ(pageIndexOf(dialog, "backupEncryptionEnabledSwitch"), 2);
}

TEST(SettingsDialogLayoutTest, AutoBackupEnabledSwitchIsOnTheBackupPage) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  EXPECT_EQ(pageIndexOf(dialog, "autoBackupEnabledSwitch"), 2);
}

TEST(SettingsDialogLayoutTest, NotificationsEnabledSwitchIsOnThePrivacyPage) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  EXPECT_EQ(pageIndexOf(dialog, "notificationsEnabledSwitch"), 1);
}

TEST(SettingsDialogLayoutTest, AppLockEnabledSwitchIsOnThePrivacyPage) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  EXPECT_EQ(pageIndexOf(dialog, "appLockEnabledSwitch"), 1);
}

TEST(SettingsDialogLayoutTest, LiveKitSaveButtonIsOnTheLastPage) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  EXPECT_EQ(pageIndexOf(dialog, "liveKitSaveButton"), 5);
}

TEST(SettingsDialogLayoutTest, SchedulingAndBillingBoxesAreBothOnTheEventsPage) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  EXPECT_EQ(pageIndexOf(dialog, "preventOverlapsSwitch"), 3);
  EXPECT_EQ(pageIndexOf(dialog, "currencyCombo"), 3);
  EXPECT_EQ(pageIndexOf(dialog, "workEventColorEditor"), 3);
}

TEST(SettingsDialogLayoutTest, SchedulingBillingAndColorsAreThreeSeparateGroupBoxes) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  auto *overlapsSwitch = dialog.findChild<QWidget *>("preventOverlapsSwitch");
  auto *currencyCombo = dialog.findChild<QWidget *>("currencyCombo");
  auto *colorEditor = dialog.findChild<QWidget *>("workEventColorEditor");
  ASSERT_NE(overlapsSwitch, nullptr);
  ASSERT_NE(currencyCombo, nullptr);
  ASSERT_NE(colorEditor, nullptr);

  auto groupBoxOf = [](QWidget *w) -> QWidget * {
    while (w != nullptr && qobject_cast<QGroupBox *>(w) == nullptr) {
      w = w->parentWidget();
    }
    return w;
  };
  QWidget *schedulingBox = groupBoxOf(overlapsSwitch);
  QWidget *billingBox = groupBoxOf(currencyCombo);
  QWidget *colorsBox = groupBoxOf(colorEditor);
  ASSERT_NE(schedulingBox, nullptr);
  ASSERT_NE(billingBox, nullptr);
  ASSERT_NE(colorsBox, nullptr);
  EXPECT_NE(schedulingBox, billingBox);
  EXPECT_NE(billingBox, colorsBox);
  EXPECT_NE(schedulingBox, colorsBox);
}

int main(int argc, char **argv) {
  // Redirect the config home to a scratch directory so SettingsDialog never
  // reads or writes the developer's real Sessio config.
  QTemporaryDir isolatedHome;
  qputenv("XDG_CONFIG_HOME", isolatedHome.path().toUtf8());
  qputenv("HOME", isolatedHome.path().toUtf8());

  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(SettingsDialogLayoutTest, EveryPageIsAVerticallyScrollableScrollArea) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  auto *stack = dialog.findChild<QStackedWidget *>();
  ASSERT_NE(stack, nullptr);
  ASSERT_EQ(stack->count(), 6);
  for (int i = 0; i < stack->count(); ++i) {
    auto *scrollArea = qobject_cast<QScrollArea *>(stack->widget(i));
    ASSERT_NE(scrollArea, nullptr) << "page " << i << " is not a QScrollArea";
    EXPECT_TRUE(scrollArea->widgetResizable());
    EXPECT_EQ(scrollArea->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    EXPECT_EQ(scrollArea->frameShape(), QFrame::NoFrame);
  }
}

TEST(SettingsDialogLayoutTest, DialogHeightNeverExceedsTheAvailableScreenHeight) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  const auto *screen = QGuiApplication::primaryScreen();
  ASSERT_NE(screen, nullptr);
  EXPECT_LE(dialog.height(), screen->availableGeometry().height());
}

TEST(SettingsDialogLayoutTest, TallPagesScrollInsteadOfOverflowingTheDialog) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  dialog.resize(560, 300);
  dialog.show();
  auto *stack = dialog.findChild<QStackedWidget *>();
  ASSERT_NE(stack, nullptr);
  auto *backupPage = qobject_cast<QScrollArea *>(stack->widget(2));
  ASSERT_NE(backupPage, nullptr);
  stack->setCurrentIndex(2);
  QApplication::processEvents();
  EXPECT_GT(backupPage->verticalScrollBar()->maximum(), 0);
}
