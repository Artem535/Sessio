#pragma once

#include <QString>
#include <optional>

struct SessioJoinLink {
  QString code;
  QString passcode;
};

[[nodiscard]] std::optional<SessioJoinLink> parseSessioJoinUrl(const QString &url);
