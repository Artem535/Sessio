#include "schedule_zoneinfo.h"

#include <schedule/schedule.h>

#include <QDir>
#include <QLoggingCategory>

namespace pcm::meeting {
namespace {
Q_LOGGING_CATEGORY(logZoneinfo, "pcm.meeting.zoneinfo")

bool hasData(const QString &directory) {
  return pcm::schedule::zoneinfoDirectoryHasData(directory.toStdString());
}
} // namespace

QStringList zoneinfoCandidates(const QString &applicationDir, const QString &envOverride) {
  QStringList result;
  if (!envOverride.isEmpty()) {
    result << QDir::cleanPath(envOverride);
  }
  const QDir app(applicationDir);
  result << QDir::cleanPath(app.filePath(QStringLiteral("../share/sessio/zoneinfo")))
         << QDir::cleanPath(app.filePath(QStringLiteral("share/sessio/zoneinfo")))
         << QDir::cleanPath(app.filePath(QStringLiteral("zoneinfo")))
         << QDir::cleanPath(app.filePath(QStringLiteral("../Resources/zoneinfo")));
  return result;
}

QString resolveZoneinfoDirectory(const QString &applicationDir, const QString &envOverride) {
  for (const auto &candidate : zoneinfoCandidates(applicationDir, envOverride)) {
    if (hasData(candidate)) {
      return candidate;
    }
  }
  return {};
}

bool configureScheduleZoneinfo(const QString &applicationDir) {
  const auto resolved =
      resolveZoneinfoDirectory(applicationDir, qEnvironmentVariable("SESSIO_ZONEINFO_DIR"));
  if (!resolved.isEmpty()) {
    pcm::schedule::setZoneinfoDirectory(resolved.toStdString());
    qCInfo(logZoneinfo) << "Using timezone data from" << resolved;
    return true;
  }
  const auto fallback = QString::fromStdString(pcm::schedule::zoneinfoDirectory());
  const bool ok = hasData(fallback);
  if (!ok) {
    qCWarning(logZoneinfo) << "No timezone data found next to the application; recurring "
                              "calls can not be published (looked in"
                           << zoneinfoCandidates(applicationDir, {}) << "and" << fallback << ")";
  }
  return ok;
}

} // namespace pcm::meeting
