#pragma once
#include <QUrl>
#include <optional>

namespace pcm::meeting {
struct JitsiAddress { QUrl server; QString room; };
std::optional<QUrl> parseJitsiServerUrl(const QString &input);
std::optional<JitsiAddress> parseJitsiRoomUrl(const QString &input);
QString createJitsiRoomUrl(const QUrl &server);
}
