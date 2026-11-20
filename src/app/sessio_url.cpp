#include "sessio_url.h"
#include "../widgets/meeting_utils.h"

#include <QUrl>
#include <QUrlQuery>

std::optional<SessioJoinLink> parseSessioJoinUrl(const QString &url) {
  const QUrl parsed(url);
  const bool browser = parsed.scheme() == QStringLiteral("https") &&
      parsed.host() == qEnvironmentVariable("SESSIO_CALL_HOST", "calls.sessio-pcm.ru") &&
      parsed.path() == QStringLiteral("/join") && parsed.userInfo().isEmpty() &&
      (parsed.port() == -1 || parsed.port() == 443) && parsed.query().isEmpty();
  if (!browser && (parsed.scheme() != QStringLiteral("sessio") || parsed.host() != QStringLiteral("join"))) {
    return std::nullopt;
  }
  const QUrlQuery query = browser ? QUrlQuery(parsed.fragment(QUrl::FullyEncoded)) : QUrlQuery(parsed);
  if (!query.hasQueryItem("code") || !query.hasQueryItem("passcode")) {
    return std::nullopt;
  }
  SessioJoinLink link;
  link.code = query.queryItemValue("code");
  link.passcode = query.queryItemValue("passcode");
  if (link.code.isEmpty() || link.passcode.isEmpty()) return std::nullopt;
  if (browser) {
    int codes = 0, passcodes = 0;
    for (const auto &item : query.queryItems()) {
      if (item.first == "code") ++codes;
      if (item.first == "passcode") ++passcodes;
      if (item.first == "backend") return std::nullopt;
    }
    if (codes != 1 || passcodes != 1) return std::nullopt;
    return link;
  }
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
