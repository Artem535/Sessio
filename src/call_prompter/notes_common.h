#pragma once

#include "prompter_pipeline.h"
#include <QJsonArray>
#include <QList>
#include <QSet>

// Building blocks shared by the notes pipelines (live points and the meeting
// summary): source preflight and chunking, evidence-checked items, prompts.
namespace pcm::prompter::notes {

// Each serialized model payload stays within 2400 UTF-8 bytes, conservative
// for the 4096-token context; the byte fallback is a token ceiling.
constexpr qsizetype kPayloadBytes = 2400;
// Input capacity after the 1024-token output and 128-token framing reserves
// (GemmaEngine allows 1024 output tokens for schema-constrained calls).
constexpr qsizetype kContextInputBytes = 4096 - 1024 - 128;
constexpr qsizetype kMaxEvidenceIds = 8;

QByteArray serialize(const QJsonObject &object);
// Stable key for duplicate facts; retains words, numbers and negations.
QString pointKey(const QString &text);
// A prompt from the :/call-prompter resources. Throws PipelineError if missing.
QString prompt(const char *name);
// One {text, evidence_ids} reply item: text <= limit, 1-8 unique IDs from
// `allowed` (none for an empty text when allowEmpty). Returns it trimmed;
// throws PipelineError("invalid notes ...") otherwise.
QJsonObject checkedItem(const QJsonValue &value, qsizetype limit, bool allowEmpty, const QSet<QString> &allowed);
// Validates every source (1024 sources, id <=80 / speaker <=200 / text <=6000
// UTF-16 units, 256000 total) and splits them into chunks whose serialized
// {"utterances":[...]} fits payloadBudget, splitting long texts with their IDs
// unchanged. At most 512 chunks. Throws PipelineError on any violation.
QList<QJsonArray> chunks(const QJsonArray &utterances, qsizetype payloadBudget);
// The source IDs of one chunk.
QSet<QString> ids(const QJsonArray &chunk);

} // namespace pcm::prompter::notes
