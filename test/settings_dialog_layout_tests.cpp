#include "settings_dialog.h"
#include "fake_token_backend_credential_store.h"

#include <QApplication>
#include <QPushButton>
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
