#include "app_role_switcher.h"
#include "../config/config.h"

#include <QCoreApplication>

#include <exception>
#include <utility>

namespace pcm {

AppRoleSwitcher::AppRoleSwitcher(Restarter restarter) : mRestarter(std::move(restarter)) {}

RoleSwitchResult AppRoleSwitcher::switchTo(const config::AppRole role) const {
  RoleSwitchResult result;
  if (role == config::AppRole::Unset) {
    result.error = QCoreApplication::translate("AppRoleSwitcher", "Invalid application mode.");
    return result;
  }

  config::Config conf;
  try {
    conf = config::Config::read_config();
  } catch (const std::exception &) {
    // Saving defaults over an unreadable file would drop the database path and
    // the token backend URL, so refuse the switch.
    result.error = QCoreApplication::translate(
        "AppRoleSwitcher", "The settings file could not be read, so the mode was not changed.");
    return result;
  }

  const auto target = config::appRoleToString(role).toStdString();
  if (conf.app_role == target) {
    result.status = RoleSwitchResult::Status::AlreadyActive;
    return result;
  }

  conf.app_role = target;
  try {
    config::Config::save_config(conf);
  } catch (const std::exception &) {
    result.error = QCoreApplication::translate(
        "AppRoleSwitcher", "The settings file could not be written, so the mode was not changed.");
    return result;
  }

  if (!mRestarter || !mRestarter()) {
    result.status = RoleSwitchResult::Status::RestartFailed;
    result.error = QCoreApplication::translate(
        "AppRoleSwitcher",
        "The mode was changed but the application could not restart. Start it again manually.");
    return result;
  }
  result.status = RoleSwitchResult::Status::Restarting;
  return result;
}

} // namespace pcm
