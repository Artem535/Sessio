#pragma once

#include "app_role.h"

#include <QString>

class QWidget;

namespace pcm {

class AppRoleSwitcher;

// Object names of the confirmation dialog, shared with tests.
inline constexpr auto kRoleSwitchConfirmationName = "roleSwitchConfirmation";
inline constexpr auto kRoleSwitchConfirmButtonName = "roleSwitchConfirmButton";

// Text of the confirmation for switching to `target`.
[[nodiscard]] QString roleSwitchTitle(config::AppRole target);
[[nodiscard]] QString roleSwitchExplanation(config::AppRole target);

// Asks the user to confirm the switch (modal). Nothing is changed unless they
// confirm; then the role is stored and the application restarted through
// `switcher`. A failed switch is reported in a message box and leaves the role
// unchanged. Returns true when a restart was started.
bool confirmAndSwitchRole(QWidget *parent, const AppRoleSwitcher &switcher,
                          config::AppRole target);

} // namespace pcm
