#include "sessio_url.h"
#include "../widgets/meeting_utils.h"

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
  // FullyDecoded so a percent-encoded backend URL (the only way to carry one
  // containing '?', '&' or '=' inside this query) comes back as a plain URL.
  // An empty or non-http(s) value is dropped rather than failing the whole
  // link: the code/passcode are still usable against the configured backend.
  const QString backend = query.queryItemValue("backend", QUrl::FullyDecoded).trimmed();
  if (pcm::meeting::isValidMeetingUrl(backend)) {
    link.backendUrl = backend;
  }
  return link;
}
