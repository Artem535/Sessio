#pragma once

#include <QString>
#include <QStringList>

namespace pcm::meeting {

// The recurring-schedule engine (pcm_schedule) resolves timezones from libical's
// own zone files. Installed builds ship them next to the executable; the path
// compiled into pcm_schedule points into the build tree and does not exist on a
// user's machine.
//
// Candidates, in priority order (first one holding zone data wins):
//   1. $SESSIO_ZONEINFO_DIR (override for support and tests)
//   2. <appDir>/../share/sessio/zoneinfo   RPM, AppImage, `cmake --install`
//   3. <appDir>/share/sessio/zoneinfo
//   4. <appDir>/zoneinfo                   Windows installer
//   5. <appDir>/../Resources/zoneinfo      macOS bundle
[[nodiscard]] QStringList zoneinfoCandidates(const QString &applicationDir,
                                             const QString &envOverride);
// First candidate that contains zone data, or an empty string.
[[nodiscard]] QString resolveZoneinfoDirectory(const QString &applicationDir,
                                               const QString &envOverride);
// Points pcm_schedule at the resolved directory. When no candidate has data the
// compiled-in path is kept (development trees). Returns whether the directory
// pcm_schedule will use contains zone data; false means every named timezone is
// rejected, so recurring series can not be published.
bool configureScheduleZoneinfo(const QString &applicationDir);

} // namespace pcm::meeting
