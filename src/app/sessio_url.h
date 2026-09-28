#pragma once

#include <QString>
#include <optional>

struct SessioJoinLink {
  QString code;
  QString passcode;
  // The token backend the invitation was issued by, from the optional
  // `backend` query item. Set only when that item is present, non-empty and
  // an http(s) URL; otherwise std::nullopt (the link itself stays valid).
  std::optional<QString> backendUrl;
};

// Parses sessio://join?code=...&passcode=...[&backend=<percent-encoded URL>].
// `code` and `passcode` are required; `backend` is optional.
[[nodiscard]] std::optional<SessioJoinLink> parseSessioJoinUrl(const QString &url);
