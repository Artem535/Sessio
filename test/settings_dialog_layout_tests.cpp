#include "app_role_switcher.h"
#include "config.h"
#include "role_switch_prompt.h"
#include "settings_dialog.h"
#include "fake_token_backend_credential_store.h"

#include <QAbstractButton>
#include <QApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QGroupBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QWidget>
#include <oclero/qlementine/widgets/AbstractItemListWidget.hpp>
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

TEST(SettingsDialogLayoutTest, ClipboardSettingsAreInTheirOwnGroupBoxOnThePrivacyPage) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  EXPECT_EQ(pageIndexOf(dialog, "clearSensitiveClipboardSwitch"), 1);
  auto *clipboardSwitch = dialog.findChild<QWidget *>("clearSensitiveClipboardSwitch");
  auto *appLockSwitch = dialog.findChild<QWidget *>("appLockEnabledSwitch");
  ASSERT_NE(clipboardSwitch, nullptr);
  ASSERT_NE(appLockSwitch, nullptr);
  auto groupBoxOf = [](QWidget *w) -> QWidget * {
    while (w != nullptr && qobject_cast<QGroupBox *>(w) == nullptr) {
      w = w->parentWidget();
    }
    return w;
  };
  QWidget *clipboardBox = groupBoxOf(clipboardSwitch);
  QWidget *appLockBox = groupBoxOf(appLockSwitch);
  ASSERT_NE(clipboardBox, nullptr);
  ASSERT_NE(appLockBox, nullptr);
  EXPECT_NE(clipboardBox, appLockBox);
}

TEST(SettingsDialogLayoutTest, SegmentedControlHasOneItemPerStackedPage) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  auto *stack = dialog.findChild<QStackedWidget *>();
  auto *sections = dialog.findChild<oclero::qlementine::AbstractItemListWidget *>();
  ASSERT_NE(stack, nullptr);
  ASSERT_NE(sections, nullptr);
  EXPECT_EQ(sections->itemCount(), stack->count());
}

TEST(SettingsDialogLayoutTest, HeightForAvailableScreenNeverExceedsTheScreen) {
  struct Case {
    int available;
    int expected;
  };
  for (const Case c : {Case{300, 300}, Case{480, 400}, Case{600, 520},
                       Case{800, 720}, Case{1080, 760}}) {
    const int height = SettingsDialog::heightForAvailableScreen(c.available);
    EXPECT_LE(height, c.available) << "available " << c.available;
    EXPECT_EQ(height, c.expected) << "available " << c.available;
  }
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

namespace {
void answerRoleConfirmationLater(const bool confirm, int *confirmations) {
  auto *timer = new QTimer;
  timer->setInterval(5);
  QObject::connect(timer, &QTimer::timeout, timer, [timer, confirm, confirmations]() {
    auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
    if (box == nullptr ||
        box->objectName() != QString::fromLatin1(pcm::kRoleSwitchConfirmationName)) {
      return;
    }
    ++*confirmations;
    if (confirm) {
      box->findChild<QAbstractButton *>(QString::fromLatin1(pcm::kRoleSwitchConfirmButtonName))
          ->click();
    } else {
      box->button(QMessageBox::Cancel)->click();
    }
    timer->deleteLater();
  });
  timer->start();
}
} // namespace

TEST(SettingsDialogLayoutTest, SwitchToClientModeButtonIsOnTheGeneralPage) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  EXPECT_EQ(pageIndexOf(dialog, "switchToClientModeButton"), 0);
  auto *button = dialog.findChild<QPushButton *>("switchToClientModeButton");
  ASSERT_NE(button, nullptr);
  EXPECT_EQ(button->text(), QString::fromUtf8("Switch to client mode\u2026"));
  // No switcher injected (e.g. embedded without an Application): disabled.
  EXPECT_FALSE(button->isEnabled());
}

TEST(SettingsDialogLayoutTest, SwitchToClientModeConfirmsStoresClientRoleAndRestartsOnce) {
  pcm::config::Config seed;
  seed.app_role = "Specialist";
  pcm::config::Config::save_config(seed);
  int restarts = 0;
  pcm::AppRoleSwitcher switcher([&restarts]() {
    ++restarts;
    return true;
  });
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  dialog.setRoleSwitcher(&switcher);
  auto *button = dialog.findChild<QPushButton *>("switchToClientModeButton");
  ASSERT_NE(button, nullptr);
  ASSERT_TRUE(button->isEnabled());

  int confirmations = 0;
  answerRoleConfirmationLater(true, &confirmations);
  button->click();

  EXPECT_EQ(confirmations, 1);
  EXPECT_EQ(restarts, 1);
  EXPECT_EQ(pcm::config::Config::read_config().app_role, "Client");
  EXPECT_EQ(dialog.result(), QDialog::Rejected); // dialog closed itself
}

TEST(SettingsDialogLayoutTest, CancellingSwitchToClientModeKeepsSpecialistRole) {
  pcm::config::Config seed;
  seed.app_role = "Specialist";
  pcm::config::Config::save_config(seed);
  int restarts = 0;
  pcm::AppRoleSwitcher switcher([&restarts]() {
    ++restarts;
    return true;
  });
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  dialog.setRoleSwitcher(&switcher);

  int confirmations = 0;
  answerRoleConfirmationLater(false, &confirmations);
  dialog.findChild<QPushButton *>("switchToClientModeButton")->click();

  EXPECT_EQ(confirmations, 1);
  EXPECT_EQ(restarts, 0);
  EXPECT_EQ(pcm::config::Config::read_config().app_role, "Specialist");
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
