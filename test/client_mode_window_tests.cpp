#include "app_role_switcher.h"
#include "client_mode_settings_dialog.h"
#include "client_mode_window.h"
#include "call_page.h"
#include "fake_video_provider.h"
#include "config.h"
#include "role_switch_prompt.h"
#include "token_backend_client.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QAbstractButton>
#include <QLineEdit>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>
#include <gtest/gtest.h>

namespace {
QString configPath() {
  return QString::fromStdString(pcm::config::Config().config_pth.value().toString());
}

void removeConfig() { QFile::remove(configPath()); }
} // namespace

TEST(ClientModeWindowTest, HostsCallsPageWithNoOwnMeetingsList) {
  pcm::video::DeviceManager deviceManager;
  pcm::tokenclient::TokenBackendClient tokenClient("http://127.0.0.1:1");
  ClientModeWindow window(&deviceManager, &tokenClient);

  EXPECT_NE(window.findChild<QLineEdit *>("joinCodeEdit"), nullptr);
  EXPECT_EQ(window.findChild<QWidget *>("ownMeetingsList"), nullptr);
}

TEST(ClientModeWindowTest, PractitionerDisplayRolesCannotExposeNotesInGroupCall) {
  pcm::video::DeviceManager devices;
  pcm::tokenclient::TokenBackendClient tokenClient("http://127.0.0.1:1");
  ClientModeWindow window(&devices, &tokenClient);
  auto *page = window.findChild<CallPage *>();
  ASSERT_NE(page, nullptr);
  auto *provider = new pcm::video::test::FakeVideoProvider;
  pcm::video::VideoSession session(provider);
  page->attachSession(&session);
  provider->simulateParticipantJoined({"local", "Synthetic local", "practitioner", true});
  provider->simulateParticipantJoined({"first", "Synthetic first", "practitioner"});
  provider->simulateParticipantJoined({"second", "Synthetic second", "practitioner"});
  QApplication::processEvents();
  EXPECT_NE(page->findChild<QWidget *>("participantTile_local"), nullptr);
  EXPECT_NE(page->findChild<QWidget *>("participantTile_first"), nullptr);
  EXPECT_NE(page->findChild<QWidget *>("participantTile_second"), nullptr);
  EXPECT_EQ(page->findChild<QWidget *>("notesToggleButton"), nullptr);
  EXPECT_EQ(window.findChild<QWidget *>("ownMeetingsList"), nullptr);
  provider->simulateParticipantJoined({"second", "Renamed", "client"});
  QApplication::processEvents();
  EXPECT_EQ(page->findChild<QWidget *>("notesToggleButton"), nullptr);
}

TEST(ClientModeWindowTest, SettingsActionRetargetsTokenClientAndPersistsUrl) {
  removeConfig();
  pcm::video::DeviceManager deviceManager;
  pcm::tokenclient::TokenBackendClient tokenClient("");
  ClientModeWindow window(&deviceManager, &tokenClient);

  auto *settingsAction = window.findChild<QAction *>("clientModeSettingsAction");
  ASSERT_NE(settingsAction, nullptr);

  // The action opens a modal dialog; fill it in and accept from inside its
  // event loop, the way a user would.
  bool dialogSeen = false;
  QTimer::singleShot(0, &window, [&window, &dialogSeen]() {
    auto *dialog = window.findChild<ClientModeSettingsDialog *>();
    if (dialog == nullptr) {
      return;
    }
    dialogSeen = true;
    auto *urlEdit = dialog->findChild<QLineEdit *>("tokenBackendUrlEdit");
    if (urlEdit == nullptr) {
      dialog->reject();
      return;
    }
    urlEdit->setText("  https://token.example.test/  ");
    dialog->accept();
  });
  settingsAction->trigger();

  ASSERT_TRUE(dialogSeen);
  EXPECT_EQ(tokenClient.baseUrl(), QStringLiteral("https://token.example.test"));
  EXPECT_EQ(pcm::config::Config::read_config().token_backend_base_url,
            "https://token.example.test");
  removeConfig();
}

namespace {
// Answers the role-switch confirmation that the menu action opens.
void answerConfirmationLater(bool confirm, int *confirmations = nullptr) {
  auto *timer = new QTimer;
  timer->setInterval(5);
  QObject::connect(timer, &QTimer::timeout, timer, [timer, confirm, confirmations]() {
    auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
    if (box == nullptr ||
        box->objectName() != QString::fromLatin1(pcm::kRoleSwitchConfirmationName)) {
      return;
    }
    if (confirmations != nullptr) {
      ++*confirmations;
    }
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

TEST(ClientModeWindowTest, SwitchRoleActionIsPresentAndDisabledWithoutASwitcher) {
  pcm::video::DeviceManager deviceManager;
  pcm::tokenclient::TokenBackendClient tokenClient("");
  ClientModeWindow window(&deviceManager, &tokenClient);

  auto *action = window.findChild<QAction *>("clientModeSwitchRoleAction");
  ASSERT_NE(action, nullptr);
  EXPECT_EQ(action->text(), QString::fromUtf8("Switch to specialist mode\u2026"));
  EXPECT_FALSE(action->isEnabled());
}

TEST(ClientModeWindowTest, SwitchRoleActionConfirmsStoresSpecialistAndRestartsOnce) {
  removeConfig();
  pcm::config::Config seed;
  seed.app_role = "Client";
  seed.token_backend_base_url = "https://keep.example.test";
  pcm::config::Config::save_config(seed);
  int restarts = 0;
  pcm::AppRoleSwitcher switcher([&restarts]() {
    ++restarts;
    return true;
  });
  pcm::video::DeviceManager deviceManager;
  pcm::tokenclient::TokenBackendClient tokenClient("");
  ClientModeWindow window(&deviceManager, &tokenClient);
  window.setRoleSwitcher(&switcher);

  auto *action = window.findChild<QAction *>("clientModeSwitchRoleAction");
  ASSERT_NE(action, nullptr);
  ASSERT_TRUE(action->isEnabled());
  int confirmations = 0;
  answerConfirmationLater(true, &confirmations);
  action->trigger();

  EXPECT_EQ(confirmations, 1);
  EXPECT_EQ(restarts, 1);
  const auto saved = pcm::config::Config::read_config();
  EXPECT_EQ(saved.app_role, "Specialist");
  EXPECT_EQ(saved.token_backend_base_url, "https://keep.example.test");
  removeConfig();
}

TEST(ClientModeWindowTest, SwitchRoleActionCancelledKeepsClientRoleAndDoesNotRestart) {
  removeConfig();
  pcm::config::Config seed;
  seed.app_role = "Client";
  pcm::config::Config::save_config(seed);
  int restarts = 0;
  pcm::AppRoleSwitcher switcher([&restarts]() {
    ++restarts;
    return true;
  });
  pcm::video::DeviceManager deviceManager;
  pcm::tokenclient::TokenBackendClient tokenClient("");
  ClientModeWindow window(&deviceManager, &tokenClient);
  window.setRoleSwitcher(&switcher);

  int confirmations = 0;
  answerConfirmationLater(false, &confirmations);
  window.findChild<QAction *>("clientModeSwitchRoleAction")->trigger();

  EXPECT_EQ(confirmations, 1);
  EXPECT_EQ(restarts, 0);
  EXPECT_EQ(pcm::config::Config::read_config().app_role, "Client");
  removeConfig();
}

TEST(ClientModeSettingsDialogTest, PrefillsFromConfigAndKeepsOtherFields) {
  removeConfig();
  pcm::config::Config conf;
  conf.app_role = "Client";
  conf.token_backend_base_url = "https://saved.example.test";
  pcm::config::Config::save_config(conf);

  pcm::tokenclient::TokenBackendClient tokenClient("https://saved.example.test");
  ClientModeSettingsDialog dialog(&tokenClient);
  auto *urlEdit = dialog.findChild<QLineEdit *>("tokenBackendUrlEdit");
  ASSERT_NE(urlEdit, nullptr);
  EXPECT_EQ(urlEdit->text(), QStringLiteral("https://saved.example.test"));
  auto *nameEdit = dialog.findChild<QLineEdit *>("callDisplayNameEdit");
  ASSERT_NE(nameEdit, nullptr);
  nameEdit->setText(" Анна ");

  urlEdit->setText("http://127.0.0.1:8080");
  dialog.accept();

  EXPECT_EQ(dialog.result(), QDialog::Accepted);
  EXPECT_EQ(tokenClient.baseUrl(), QStringLiteral("http://127.0.0.1:8080"));
  const auto saved = pcm::config::Config::read_config();
  EXPECT_EQ(saved.token_backend_base_url, "http://127.0.0.1:8080");
  EXPECT_EQ(saved.app_role, "Client");
  ClientModeSettingsDialog reopened(&tokenClient);
  EXPECT_EQ(reopened.findChild<QLineEdit *>("callDisplayNameEdit")->text(), "Анна");
  removeConfig();
}

TEST(ClientModeSettingsDialogTest, InvalidUrlKeepsDialogOpenAndDoesNotRetarget) {
  removeConfig();
  pcm::tokenclient::TokenBackendClient tokenClient("https://old.example.test");
  ClientModeSettingsDialog dialog(&tokenClient);
  auto *urlEdit = dialog.findChild<QLineEdit *>("tokenBackendUrlEdit");
  auto *errorLabel = dialog.findChild<QLabel *>("tokenBackendUrlError");
  ASSERT_NE(urlEdit, nullptr);
  ASSERT_NE(errorLabel, nullptr);
  EXPECT_TRUE(errorLabel->isHidden());

  urlEdit->setText("not a url");
  dialog.accept();

  EXPECT_FALSE(errorLabel->isHidden());
  EXPECT_NE(dialog.result(), QDialog::Accepted);
  EXPECT_EQ(tokenClient.baseUrl(), QStringLiteral("https://old.example.test"));
  EXPECT_FALSE(QFile::exists(configPath()));
}

TEST(ClientModeSettingsDialogTest, CorruptConfigStillOpensWithCurrentClientUrl) {
  QDir().mkpath(QFileInfo(configPath()).absolutePath());
  {
    QFile file(configPath());
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("::: this is not [valid yaml for Config\n");
  }

  pcm::tokenclient::TokenBackendClient tokenClient("https://in-memory.example.test");
  ClientModeSettingsDialog dialog(&tokenClient);
  auto *urlEdit = dialog.findChild<QLineEdit *>("tokenBackendUrlEdit");
  ASSERT_NE(urlEdit, nullptr);
  EXPECT_EQ(urlEdit->text(), QStringLiteral("https://in-memory.example.test"));
  removeConfig();
}

int main(int argc, char **argv) {
  // Config::save_config/read_config resolve to Poco::Path::configHome(), the
  // developer's real ~/.config on Linux. Redirect it (and HOME, which macOS
  // uses instead of XDG_CONFIG_HOME) before any Config is constructed, so these
  // tests never touch the developer's actual Sessio config.
  QTemporaryDir isolatedHome;
  qputenv("XDG_CONFIG_HOME", isolatedHome.path().toUtf8());
  qputenv("HOME", isolatedHome.path().toUtf8());

  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
