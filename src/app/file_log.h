#pragma once

#include <QString>

namespace pcm::app {

// Mirrors every Qt log message into <dir>/sessio.log (rotated to
// sessio.log.1 past maxBytes) while still passing it to the previous
// handler. A Windows GUI build has no console, so without this a hang or a
// crash in the field leaves nothing to look at. Messages must stay free of
// client data like any production log (see AGENTS.md).
void installFileLog(const QString &dir, qint64 maxBytes = 2 * 1024 * 1024);

// The file installFileLog() writes to, or empty when it was not installed.
QString fileLogPath();

// Windows only: writes <dir>/sessio-crash-<time>.dmp when the process dies on
// an unhandled exception. A no-op elsewhere.
void installCrashDumpHandler(const QString &dir);

} // namespace pcm::app
