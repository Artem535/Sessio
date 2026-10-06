#include "app_role_switcher.h"
#include "application_restarter.h"
#include "config.h"
#include "role_switch_prompt.h"
#include "single_instance_guard.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>
#include <gtest/gtest.h>

#include <sys/stat.h>
#include <unistd.h>

using pcm::AppRoleSwitcher;
using pcm::ApplicationRestarter;
using pcm::RoleSwitchResult;
using pcm::config::AppRole;
using pcm::config::Config;

namespace {

QString configPath() { return QString::fromStdString(Config().config_pth.value().toString()); }

void removeConfig() {
  QFile::remove(configPath());
  QFile::remove(configPath() + QStringLiteral(".tmp"));
}

void seedConfig(const std::string &role, const std::string &tokenUrl = "https://token.example.test") {
  Config conf;
  conf.app_role = role;
  conf.token_backend_base_url = tokenUrl;
  Config::save_config(conf);
}

QByteArray readFile(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

struct CountingRestarter {
  int calls = 0;
  bool result = true;
  AppRoleSwitcher::Restarter fn() {
    return [this]() {
      ++calls;
      return result;
    };
  }
};

// Answers every modal QMessageBox that appears: the role-switch confirmation
// with `confirm` (Switch and restart) or Cancel, any other box (an error) by
// closing it, remembering its text.
class MessageBoxResponder {
public:
  explicit MessageBoxResponder(bool confirm) : mConfirm(confirm) {
    mTimer.setInterval(5);
    QObject::connect(&mTimer, &QTimer::timeout, [this]() { answer(); });
    mTimer.start();
  }
  int confirmations = 0;
  QString confirmationInformativeText;
  QStringList errorTexts;

private:
  void answer() {
    auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
    if (box == nullptr) {
      return;
    }
    if (box->objectName() == QString::fromLatin1(pcm::kRoleSwitchConfirmationName)) {
      ++confirmations;
      confirmationInformativeText = box->informativeText();
      if (mConfirm) {
        box->findChild<QAbstractButton *>(QString::fromLatin1(pcm::kRoleSwitchConfirmButtonName))
            ->click();
      } else {
        box->button(QMessageBox::Cancel)->click();
      }
    } else {
      errorTexts << box->text();
      box->accept();
    }
  }
  bool mConfirm;
  QTimer mTimer;
};

} // namespace

class AppRoleSwitcherTest : public ::testing::Test {
protected:
  void SetUp() override { removeConfig(); }
  void TearDown() override {
    QDir dir(QFileInfo(configPath()).absolutePath());
    chmod(dir.absolutePath().toUtf8().constData(), 0700);
    removeConfig();
  }
};

TEST_F(AppRoleSwitcherTest, PersistsNewRoleKeepsOtherSettingsAndRestartsExactlyOnce) {
  seedConfig("Client");
  CountingRestarter restarter;
  AppRoleSwitcher switcher(restarter.fn());

  const auto result = switcher.switchTo(AppRole::Specialist);

  EXPECT_EQ(result.status, RoleSwitchResult::Status::Restarting);
  EXPECT_EQ(restarter.calls, 1);
  const auto saved = Config::read_config();
  EXPECT_EQ(saved.app_role, "Specialist");
  EXPECT_EQ(saved.token_backend_base_url, "https://token.example.test");
}

TEST_F(AppRoleSwitcherTest, SwitchesSpecialistToClient) {
  seedConfig("Specialist");
  CountingRestarter restarter;
  EXPECT_EQ(AppRoleSwitcher(restarter.fn()).switchTo(AppRole::Client).status,
            RoleSwitchResult::Status::Restarting);
  EXPECT_EQ(Config::read_config().app_role, "Client");
  EXPECT_EQ(restarter.calls, 1);
}

TEST_F(AppRoleSwitcherTest, MissingConfigFileIsCreatedWithTheRole) {
  CountingRestarter restarter;
  EXPECT_EQ(AppRoleSwitcher(restarter.fn()).switchTo(AppRole::Specialist).status,
            RoleSwitchResult::Status::Restarting);
  EXPECT_EQ(Config::read_config().app_role, "Specialist");
}

TEST_F(AppRoleSwitcherTest, AlreadyActiveRoleWritesAndRestartsNothing) {
  seedConfig("Client");
  const auto before = readFile(configPath());
  CountingRestarter restarter;
  const auto result = AppRoleSwitcher(restarter.fn()).switchTo(AppRole::Client);
  EXPECT_EQ(result.status, RoleSwitchResult::Status::AlreadyActive);
  EXPECT_EQ(restarter.calls, 0);
  EXPECT_EQ(readFile(configPath()), before);
}

TEST_F(AppRoleSwitcherTest, UnsetRoleIsRejected) {
  seedConfig("Client");
  CountingRestarter restarter;
  const auto result = AppRoleSwitcher(restarter.fn()).switchTo(AppRole::Unset);
  EXPECT_EQ(result.status, RoleSwitchResult::Status::ConfigError);
  EXPECT_EQ(restarter.calls, 0);
  EXPECT_EQ(Config::read_config().app_role, "Client");
}

TEST_F(AppRoleSwitcherTest, UnreadableConfigIsNeverOverwrittenAndDoesNotRestart) {
  QDir().mkpath(QFileInfo(configPath()).absolutePath());
  {
    QFile file(configPath());
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("::: this is not [valid yaml for Config\n");
  }
  const auto before = readFile(configPath());
  CountingRestarter restarter;

  const auto result = AppRoleSwitcher(restarter.fn()).switchTo(AppRole::Specialist);

  EXPECT_EQ(result.status, RoleSwitchResult::Status::ConfigError);
  EXPECT_FALSE(result.error.isEmpty());
  EXPECT_EQ(restarter.calls, 0);
  EXPECT_EQ(readFile(configPath()), before);
}

TEST_F(AppRoleSwitcherTest, FailedWriteLeavesRoleUnchangedAndDoesNotRestart) {
  if (geteuid() == 0) {
    GTEST_SKIP() << "directory permissions do not stop root";
  }
  seedConfig("Client");
  const auto dir = QFileInfo(configPath()).absolutePath();
  ASSERT_EQ(chmod(dir.toUtf8().constData(), 0500), 0);
  CountingRestarter restarter;

  const auto result = AppRoleSwitcher(restarter.fn()).switchTo(AppRole::Specialist);

  chmod(dir.toUtf8().constData(), 0700);
  EXPECT_EQ(result.status, RoleSwitchResult::Status::ConfigError);
  EXPECT_FALSE(result.error.isEmpty());
  EXPECT_EQ(restarter.calls, 0);
  EXPECT_EQ(Config::read_config().app_role, "Client");
  EXPECT_FALSE(QFile::exists(configPath() + QStringLiteral(".tmp")));
}

TEST_F(AppRoleSwitcherTest, RestartFailureIsReportedButTheRoleStaysSaved) {
  seedConfig("Client");
  CountingRestarter restarter;
  restarter.result = false;
  const auto result = AppRoleSwitcher(restarter.fn()).switchTo(AppRole::Specialist);
  EXPECT_EQ(result.status, RoleSwitchResult::Status::RestartFailed);
  EXPECT_FALSE(result.error.isEmpty());
  EXPECT_EQ(restarter.calls, 1);
  EXPECT_EQ(Config::read_config().app_role, "Specialist");
}

TEST_F(AppRoleSwitcherTest, SaveConfigLeavesNoTempFileBehind) {
  seedConfig("Client");
  EXPECT_FALSE(QFile::exists(configPath() + QStringLiteral(".tmp")));
}

// ---- ApplicationRestarter ----

TEST(ApplicationRestarterTest, ReleasesSingleInstanceLockBeforeSpawningSoTheSuccessorIsPrimary) {
  SingleInstanceGuard guard;
  ASSERT_TRUE(guard.isPrimaryInstance());
  int spawns = 0;
  int quits = 0;
  bool successorWasPrimary = false;
  QString spawnedProgram;
  QStringList spawnedArgs;
  ApplicationRestarter restarter(
      &guard, "/opt/sessio/Sessio", {"--flag", "value"},
      [&](const QString &program, const QStringList &args) {
        ++spawns;
        spawnedProgram = program;
        spawnedArgs = args;
        // What the relaunched process does first: try to become primary.
        SingleInstanceGuard successor;
        successorWasPrimary = successor.isPrimaryInstance();
        return true;
      },
      [&]() { ++quits; });

  EXPECT_TRUE(restarter.restart());

  EXPECT_EQ(spawns, 1);
  EXPECT_EQ(quits, 1);
  EXPECT_TRUE(successorWasPrimary);
  EXPECT_EQ(spawnedProgram, QStringLiteral("/opt/sessio/Sessio"));
  EXPECT_EQ(spawnedArgs, (QStringList{"--flag", "value"}));
  EXPECT_FALSE(guard.isPrimaryInstance());
}

TEST(ApplicationRestarterTest, FailedSpawnTakesTheLockBackAndKeepsRunning) {
  SingleInstanceGuard guard;
  ASSERT_TRUE(guard.isPrimaryInstance());
  int quits = 0;
  ApplicationRestarter restarter(
      &guard, "/missing/Sessio", {},
      [](const QString &, const QStringList &) { return false; }, [&]() { ++quits; });

  EXPECT_FALSE(restarter.restart());

  EXPECT_EQ(quits, 0);
  EXPECT_TRUE(guard.isPrimaryInstance());
  SingleInstanceGuard other;
  EXPECT_FALSE(other.isPrimaryInstance());
}

TEST(ApplicationRestarterTest, RestartArgumentsKeepOptionsButDropStaleJoinLinks) {
  EXPECT_EQ(ApplicationRestarter::restartArguments(
                {"-platform", "offscreen", "sessio://join?code=abc&passcode=1"}),
            (QStringList{"-platform", "offscreen"}));
  EXPECT_TRUE(ApplicationRestarter::restartArguments({}).isEmpty());
}

TEST(ApplicationRestarterTest, ForRunningApplicationUsesTheCurrentExecutable) {
  QString program;
  QStringList args;
  auto probe = ApplicationRestarter::forRunningApplication(
      nullptr,
      [&](const QString &p, const QStringList &a) {
        program = p;
        args = a;
        return true;
      },
      []() {});
  EXPECT_TRUE(probe.restart());
  EXPECT_EQ(program, QCoreApplication::applicationFilePath());
  EXPECT_FALSE(program.isEmpty());
  EXPECT_EQ(args, ApplicationRestarter::restartArguments(QCoreApplication::arguments().mid(1)));
}

TEST_F(AppRoleSwitcherTest, SwitcherPlusRestarterSpawnsOnceAndQuitsOnceOnlyAfterRoleIsSaved) {
  seedConfig("Client");
  SingleInstanceGuard guard;
  ASSERT_TRUE(guard.isPrimaryInstance());
  std::string roleSeenBySuccessor;
  int spawns = 0;
  int quits = 0;
  ApplicationRestarter restarter(
      &guard, "/opt/sessio/Sessio", {},
      [&](const QString &, const QStringList &) {
        ++spawns;
        roleSeenBySuccessor = Config::read_config().app_role;
        return true;
      },
      [&]() { ++quits; });
  AppRoleSwitcher switcher([&restarter]() { return restarter.restart(); });

  EXPECT_EQ(switcher.switchTo(AppRole::Specialist).status, RoleSwitchResult::Status::Restarting);

  EXPECT_EQ(spawns, 1);
  EXPECT_EQ(quits, 1);
  EXPECT_EQ(roleSeenBySuccessor, "Specialist");
}

// ---- confirmation prompt ----

TEST_F(AppRoleSwitcherTest, ConfirmingThePromptStoresTheRoleAndRestartsOnce) {
  seedConfig("Client");
  CountingRestarter restarter;
  AppRoleSwitcher switcher(restarter.fn());
  MessageBoxResponder responder(/*confirm=*/true);

  EXPECT_TRUE(pcm::confirmAndSwitchRole(nullptr, switcher, AppRole::Specialist));

  EXPECT_EQ(responder.confirmations, 1);
  EXPECT_EQ(restarter.calls, 1);
  EXPECT_EQ(Config::read_config().app_role, "Specialist");
}

TEST_F(AppRoleSwitcherTest, CancellingThePromptChangesNothing) {
  seedConfig("Client");
  const auto before = readFile(configPath());
  CountingRestarter restarter;
  AppRoleSwitcher switcher(restarter.fn());
  MessageBoxResponder responder(/*confirm=*/false);

  EXPECT_FALSE(pcm::confirmAndSwitchRole(nullptr, switcher, AppRole::Specialist));

  EXPECT_EQ(responder.confirmations, 1);
  EXPECT_EQ(restarter.calls, 0);
  EXPECT_EQ(readFile(configPath()), before);
}

TEST_F(AppRoleSwitcherTest, SpecialistToClientPromptWarnsThatDataIsKeptNotDeleted) {
  seedConfig("Specialist");
  CountingRestarter restarter;
  AppRoleSwitcher switcher(restarter.fn());
  MessageBoxResponder responder(/*confirm=*/false);

  pcm::confirmAndSwitchRole(nullptr, switcher, AppRole::Client);

  EXPECT_TRUE(responder.confirmationInformativeText.contains(QStringLiteral("NOT deleted")))
      << responder.confirmationInformativeText.toStdString();
  EXPECT_TRUE(responder.confirmationInformativeText.contains(QStringLiteral("stays on this computer")));
}

TEST_F(AppRoleSwitcherTest, ClientToSpecialistPromptMentionsRestartAndFirstRunSetup) {
  seedConfig("Client");
  CountingRestarter restarter;
  AppRoleSwitcher switcher(restarter.fn());
  MessageBoxResponder responder(/*confirm=*/false);

  pcm::confirmAndSwitchRole(nullptr, switcher, AppRole::Specialist);

  EXPECT_TRUE(responder.confirmationInformativeText.contains(QStringLiteral("restart")));
  EXPECT_TRUE(responder.confirmationInformativeText.contains(QStringLiteral("first-run")));
}

TEST_F(AppRoleSwitcherTest, ConfigFailureAfterConfirmationShowsAnErrorAndKeepsTheRole) {
  QDir().mkpath(QFileInfo(configPath()).absolutePath());
  {
    QFile file(configPath());
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("::: not [valid yaml\n");
  }
  CountingRestarter restarter;
  AppRoleSwitcher switcher(restarter.fn());
  MessageBoxResponder responder(/*confirm=*/true);

  EXPECT_FALSE(pcm::confirmAndSwitchRole(nullptr, switcher, AppRole::Specialist));

  EXPECT_EQ(restarter.calls, 0);
  ASSERT_EQ(responder.errorTexts.size(), 1);
  EXPECT_FALSE(responder.errorTexts.first().isEmpty());
}

int main(int argc, char **argv) {
  // Config and the single-instance lock resolve under the generic config
  // location: isolate it before any Config or QCoreApplication exists.
  QTemporaryDir isolatedHome;
  qputenv("XDG_CONFIG_HOME", isolatedHome.path().toUtf8());
  qputenv("HOME", isolatedHome.path().toUtf8());

  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
