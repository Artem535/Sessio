#pragma once

#include <QString>

namespace pcm::transcriptionui {
// Only approved translated messages leave this boundary; diagnostics may contain private data.
QString userFacingFailure(const QString &raw);
}

namespace pcm::transcriptionui {
// "Cloud unavailable: …" for a cloud engine failure; `detail` is the worker's
// cloud_detail (auth, model, no_key, unsupported, rate_limit, server, network,
// timeout, request, no_engine).
QString cloudFailureText(const QString &detail);
// What produced a result: "" for results saved before engines were recorded.
QString engineLabel(const QString &raw);
// A cloud failure worth retrying later, as opposed to one the user must fix.
bool cloudFailureIsTransient(const QString &detail);
}
