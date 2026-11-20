#pragma once

#include <QString>
#include <optional>

namespace pcm::config {

enum class AppRole { Unset, Specialist, Client };

[[nodiscard]] inline QString appRoleToString(const AppRole role) {
  switch (role) {
  case AppRole::Unset:
    return QStringLiteral("Unset");
  case AppRole::Specialist:
    return QStringLiteral("Specialist");
  case AppRole::Client:
    return QStringLiteral("Client");
  }
  return QStringLiteral("Unset");
}

[[nodiscard]] inline std::optional<AppRole> appRoleFromString(const QString &value) {
  if (value == QStringLiteral("Unset")) {
    return AppRole::Unset;
  }
  if (value == QStringLiteral("Specialist")) {
    return AppRole::Specialist;
  }
  if (value == QStringLiteral("Client")) {
    return AppRole::Client;
  }
  return std::nullopt;
}

} // namespace pcm::config
