#include "role_switch_prompt.h"
#include "app_role_switcher.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QPushButton>

namespace pcm {

QString roleSwitchTitle(const config::AppRole target) {
  return target == config::AppRole::Specialist
             ? QCoreApplication::translate("RoleSwitchPrompt", "Switch to specialist mode")
             : QCoreApplication::translate("RoleSwitchPrompt", "Switch to client mode");
}

QString roleSwitchExplanation(const config::AppRole target) {
  if (target == config::AppRole::Specialist) {
    return QCoreApplication::translate(
        "RoleSwitchPrompt",
        "Sessio will restart as the specialist application. If the specialist profile is not "
        "set up yet, the specialist first-run setup runs after the restart. An active call in "
        "this window will be ended.");
  }
  return QCoreApplication::translate(
      "RoleSwitchPrompt",
      "Sessio will restart in client mode, where you can only join calls. Your specialist data "
      "(clients, events, notes and backups) stays on this computer and is NOT deleted; "
      "switch back to specialist mode at any time to see it again. The specialist windows will "
      "be closed.");
}

bool confirmAndSwitchRole(QWidget *parent, const AppRoleSwitcher &switcher,
                          const config::AppRole target) {
  QMessageBox confirmation(parent);
  confirmation.setObjectName(QString::fromLatin1(kRoleSwitchConfirmationName));
  confirmation.setIcon(QMessageBox::Question);
  confirmation.setWindowTitle(roleSwitchTitle(target));
  confirmation.setText(roleSwitchTitle(target) + QStringLiteral("?"));
  confirmation.setInformativeText(roleSwitchExplanation(target));
  auto *confirmButton = confirmation.addButton(
      QCoreApplication::translate("RoleSwitchPrompt", "Switch and restart"),
      QMessageBox::AcceptRole);
  confirmButton->setObjectName(QString::fromLatin1(kRoleSwitchConfirmButtonName));
  auto *cancelButton = confirmation.addButton(QMessageBox::Cancel);
  confirmation.setDefaultButton(cancelButton);
  confirmation.setEscapeButton(cancelButton);
  confirmation.exec();
  if (confirmation.clickedButton() != confirmButton) {
    return false;
  }

  const auto result = switcher.switchTo(target);
  switch (result.status) {
  case RoleSwitchResult::Status::Restarting:
    return true;
  case RoleSwitchResult::Status::AlreadyActive:
    return false;
  case RoleSwitchResult::Status::ConfigError:
    QMessageBox::critical(parent, roleSwitchTitle(target), result.error);
    return false;
  case RoleSwitchResult::Status::RestartFailed:
    QMessageBox::warning(parent, roleSwitchTitle(target), result.error);
    return false;
  }
  return false;
}

} // namespace pcm
