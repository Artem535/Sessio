#pragma once

#include <QString>

namespace pcm::transcriptionui {
// Only approved translated messages leave this boundary; diagnostics may contain private data.
QString userFacingFailure(const QString &raw);
}
