#pragma once

#include "app_role.h"

#include <QString>

#include <functional>

namespace pcm {

// Outcome of AppRoleSwitcher::switchTo().
struct RoleSwitchResult {
  enum class Status {
    // The new role is stored and the restart was handed to the restarter.
    Restarting,
    // The stored role already equals the requested one: nothing was written
    // and no restart was requested.
    AlreadyActive,
    // Config could not be read or written: the stored role is unchanged and
    // no restart was requested.
    ConfigError,
    // The role is stored but the restarter could not relaunch the
    // application; the new role applies on the next manual start.
    RestartFailed,
  };

  Status status = Status::ConfigError;
  // Human-readable detail for ConfigError/RestartFailed. Never contains
  // config contents, only the failure reason.
  QString error;

  [[nodiscard]] bool roleChanged() const {
    return status == Status::Restarting || status == Status::RestartFailed;
  }
};

// Persists a new pcm::config::AppRole and asks for an application restart.
//
// The role is stored through Config::read_config()/save_config() (the latter
// writes via a temp file + rename). An unreadable Config is never replaced by
// defaults -- that would wipe the database path and token backend URL -- so
// the switch is refused instead. The restarter runs only after the new role
// is safely on disk, and exactly once per successful switch.
class AppRoleSwitcher final {
public:
  // Returns true when the new process was launched and this one is going away.
  using Restarter = std::function<bool()>;

  explicit AppRoleSwitcher(Restarter restarter);

  [[nodiscard]] RoleSwitchResult switchTo(config::AppRole role) const;

private:
  Restarter mRestarter;
};

} // namespace pcm
