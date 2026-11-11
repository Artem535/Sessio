#pragma once

#include "prompter_pipeline.h"
#include <QJsonArray>
#include <QStringList>

namespace pcm::prompter {

// Incremental key points of a running call. Reads only `newUtterances` (the
// phrases after the last successful run); `knownPoints` are the texts of the
// points found so far, sent as reference so the model does not repeat them,
// never as citable sources (only the 10 most recent are sent).
// Result: {key_points:[{text,evidence_ids}]}, 0-3 points per chunk, text <=80
// UTF-16 units, 1-8 unique IDs of that chunk's sources. Every call passes a
// reflect-cpp JSON schema whose evidence_ids may only name that chunk's IDs.
// A chunk whose reply is unusable is skipped; when every chunk failed this
// throws PipelineError("invalid notes output"). Empty input returns no points
// without calling the model. Source limits and other errors are as in
// notes::chunks(); generator exceptions propagate unchanged.
QJsonObject processLivePoints(const QJsonArray &newUtterances, const QStringList &knownPoints,
                              const Generate &generate);

} // namespace pcm::prompter
