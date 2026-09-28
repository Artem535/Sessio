#include "sessio_url.h"

#include <QUrl>
#include <QUrlQuery>

std::optional<SessioJoinLink> parseSessioJoinUrl(const QString &url) {
  const QUrl parsed(url);
  if (parsed.scheme() != QStringLiteral("sessio") || parsed.host() != QStringLiteral("join")) {
    return std::nullopt;
  }
  const QUrlQuery query(parsed);
  if (!query.hasQueryItem("code") || !query.hasQueryItem("passcode")) {
    return std::nullopt;
  }
  SessioJoinLink link;
  link.code = query.queryItemValue("code");
  link.passcode = query.queryItemValue("passcode");
  return link;
}
