#pragma once

#include "prompter_pipeline.h"
#include <QJsonArray>

namespace pcm::prompter {

// Generates an evidence-linked Russian draft, never accepted advice or a
// diagnosis. Every source has id/speaker/text/start_ms/end_ms. All source text
// is delivered, splitting long utterances with their original IDs unchanged.
// Result: {note:{text,evidence_ids}, key_points:[{text,evidence_ids}]}.
// Empty input returns an empty note and points without calling generate.
//
// Limits: 1024 unique sources, id <=80 / speaker <=200 / text <=6000
// UTF-16 units per source, 256000 total text units;
// <=512 leaf chunks, <=1023 calls including pairwise reduction. Each serialized
// payload is <=2400 UTF-8 bytes (conservative for a 4096-token context).
// Chunking also subtracts the actual prompt bytes and the 384 output / 128
// framing reserves from that context; prompt growth produces smaller chunks.
// Model outputs: note <=160 units, <=4 points of <=80 units, <=8 source IDs
// per item, <=1100 serialized UTF-8 bytes total, including all JSON overhead.
// Each nonempty item must cite sources present in that specific model input.
// Every call passes a JSON schema of this shape (from reflect-cpp) whose
// evidence_ids may only name that call's sources, for constrained decoding.
// An output that breaks these rules is never used. A rejected chunk summary is
// retried once as two halves and a half that fails again is left out; a
// rejected merge keeps both validated summaries' items, cut to the budget.
// Throws PipelineError on invalid input, budget exhaustion, or when no chunk
// produced a usable summary. Other generator exceptions propagate unchanged.
// The caller checks cancellation/revision/consent at EVERY Generate boundary
// and before publishing the returned object.
QJsonObject processNotes(const QJsonArray &utterances, const Generate &generate);

} // namespace pcm::prompter
