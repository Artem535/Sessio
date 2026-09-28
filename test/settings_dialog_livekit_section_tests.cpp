#include "settings_dialog.h"
#include "fake_token_backend_credential_store.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <gtest/gtest.h>

#include <memory>

TEST(SettingsDialogLiveKitSectionTest, SavingWritesBaseUrlToConfigAndCredentialToStore) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);

  auto *baseUrlEdit = dialog.findChild<QLineEdit *>("liveKitBaseUrlEdit");
  auto *credentialEdit = dialog.findChild<QLineEdit *>("liveKitBearerCredentialEdit");
  auto *saveButton = dialog.findChild<QPushButton *>("liveKitSaveButton");
  ASSERT_NE(baseUrlEdit, nullptr);
  ASSERT_NE(credentialEdit, nullptr);
  ASSERT_NE(saveButton, nullptr);

  baseUrlEdit->setText("https://token.example.test");
  credentialEdit->setText("bearer-secret");
  QTest::mouseClick(saveButton, Qt::LeftButton);

  const auto savedConfig = pcm::config::Config::read_config();
  EXPECT_EQ(savedConfig.token_backend_base_url, "https://token.example.test");
  EXPECT_TRUE(credentialStore->mHasCredential);
  EXPECT_EQ(credentialStore->mCredential, QStringLiteral("bearer-secret"));
}

namespace {
constexpr auto kCorruptConfig = "::: this is not [valid yaml for Config\n";

// Writes an unparseable Config.yaml at the (isolated, see main()) path
// Config::read_config() uses, and removes it again on destruction so later
// tests start from a clean config.
class CorruptConfigFile final {
public:
  CorruptConfigFile()
      : mPath(QString::fromStdString(pcm::config::Config().config_pth.value().toString())) {
    QDir().mkpath(QFileInfo(mPath).absolutePath());
    QFile file(mPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      file.write(kCorruptConfig);
    }
  }
  ~CorruptConfigFile() { QFile::remove(mPath); }
  CorruptConfigFile(const CorruptConfigFile &) = delete;
  CorruptConfigFile &operator=(const CorruptConfigFile &) = delete;

  [[nodiscard]] QByteArray contents() const {
    QFile file(mPath);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
  }

private:
  QString mPath;
};

// Closes the next QMessageBox that opens (a static QMessageBox::warning
// blocks in its own event loop) and records its text.
void closeNextMessageBox(QString *text) {
  auto *timer = new QTimer();
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, [timer, text]() {
    if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
      *text = box->text();
      timer->stop();
      timer->deleteLater();
      box->accept();
    }
  });
  timer->start();
}
} // namespace

TEST(SettingsDialogLiveKitSectionTest, CorruptConfigLeavesBaseUrlEmptyInsteadOfCrashing) {
  const CorruptConfigFile corruptConfig;
  EXPECT_ANY_THROW(pcm::config::Config::read_config());

  auto *credentialStore = new FakeTokenBackendCredentialStore();
  std::unique_ptr<SettingsDialog> dialog;
  EXPECT_NO_THROW(dialog = std::make_unique<SettingsDialog>(nullptr, credentialStore));
  ASSERT_NE(dialog, nullptr);

  auto *baseUrlEdit = dialog->findChild<QLineEdit *>("liveKitBaseUrlEdit");
  ASSERT_NE(baseUrlEdit, nullptr);
  EXPECT_TRUE(baseUrlEdit->text().isEmpty());
}

TEST(SettingsDialogLiveKitSectionTest, SavingOverCorruptConfigWarnsAndLeavesFileUntouched) {
  const CorruptConfigFile corruptConfig;

  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);
  auto *baseUrlEdit = dialog.findChild<QLineEdit *>("liveKitBaseUrlEdit");
  auto *credentialEdit = dialog.findChild<QLineEdit *>("liveKitBearerCredentialEdit");
  auto *saveButton = dialog.findChild<QPushButton *>("liveKitSaveButton");
  ASSERT_NE(saveButton, nullptr);

  baseUrlEdit->setText("https://token.example.test");
  credentialEdit->setText("bearer-secret");
  QString warningText;
  closeNextMessageBox(&warningText);
  QTest::mouseClick(saveButton, Qt::LeftButton);

  EXPECT_FALSE(warningText.isEmpty());
  // Not replaced by a default-constructed Config (which would reset app_role).
  EXPECT_EQ(corruptConfig.contents(), QByteArray(kCorruptConfig));
  // The keychain credential is independent of Config.yaml and still saved.
  EXPECT_TRUE(credentialStore->mHasCredential);
  EXPECT_EQ(credentialStore->mCredential, QStringLiteral("bearer-secret"));
}

int main(int argc, char **argv) {
  // Config::save_config/read_config resolve to Poco::Path::configHome(),
  // which is the developer's real ~/.config on Linux. Redirect it (and HOME,
  // since macOS ignores XDG_CONFIG_HOME) to a scratch directory before any
  // Config or SettingsDialog is constructed, so this test never overwrites
  // the developer's actual Sessio config.
  QTemporaryDir isolatedHome;
  qputenv("XDG_CONFIG_HOME", isolatedHome.path().toUtf8());
  qputenv("HOME", isolatedHome.path().toUtf8());

  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
