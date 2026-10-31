#include "jitsi_url.h"
#include <QUuid>
#include <QHostAddress>

namespace pcm::meeting {
namespace {
std::optional<QUrl> checkedUrl(const QString &input) {
  QUrl url(input.trimmed(), QUrl::StrictMode);
  const auto host = url.host().toLower();
  const bool loopback = host == "localhost" || QHostAddress(host).isLoopback();
  if (!url.isValid() || host.isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() ||
      url.hasFragment() || (url.scheme() != "https" && !(url.scheme() == "http" && loopback)))
    return std::nullopt;
  // Encoded separators and dot segments obscure the room/deployment boundary.
  const auto path = url.path(QUrl::FullyEncoded);
  if (path.contains("%2f", Qt::CaseInsensitive) || path.contains("%5c", Qt::CaseInsensitive) ||
      url.path().contains('\\')) return std::nullopt;
  for (const auto &segment : url.path().split('/'))
    if (segment == "." || segment == "..") return std::nullopt;
  return url;
}
}
std::optional<QUrl> parseJitsiServerUrl(const QString &input) {
  auto url = checkedUrl(input);
  if (!url) return std::nullopt;
  auto path = url->path();
  if (!path.endsWith('/')) path += '/';
  url->setPath(path);
  return url;
}
std::optional<JitsiAddress> parseJitsiRoomUrl(const QString &input) {
  auto url = checkedUrl(input);
  if (!url) return std::nullopt;
  auto path = url->path();
  while (path.endsWith('/')) path.chop(1);
  const auto slash = path.lastIndexOf('/');
  const auto room = path.mid(slash + 1);
  if (slash < 0 || room.trimmed().isEmpty()) return std::nullopt;
  url->setPath(path.left(slash + 1));
  return JitsiAddress{*url, room};
}
QString createJitsiRoomUrl(const QUrl &server) {
  QUrl url(server);
  url.setPath(server.path() + "sessio-" + QUuid::createUuid().toString(QUuid::Id128));
  return url.toString(QUrl::FullyEncoded);
}
}
