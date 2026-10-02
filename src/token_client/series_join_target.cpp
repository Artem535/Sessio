#include "series_join_target.h"

#include "token_backend_client.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QTimeZone>

namespace pcm::meeting {
namespace {
constexpr auto kPrefix = "series:";
}

QString encodeSeriesJoinTarget(const QString &seriesUid, const qint64 originalStartMs) {
  return QString::fromLatin1(kPrefix) + seriesUid.toLower() + QLatin1Char('@') +
         pcm::tokenclient::formatOriginalStartUtc(originalStartMs);
}

std::optional<SeriesJoinTarget> parseSeriesJoinTarget(const QString &reference) {
  static const QRegularExpression pattern(QStringLiteral(
      "^series:([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})@"
      "(\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}Z)$"));
  const auto match = pattern.match(reference);
  if (!match.hasMatch()) {
    return std::nullopt;
  }
  const auto start = QDateTime::fromString(match.captured(2), Qt::ISODate);
  if (!start.isValid()) {
    return std::nullopt;
  }
  return SeriesJoinTarget{match.captured(1), start.toMSecsSinceEpoch()};
}

} // namespace pcm::meeting
