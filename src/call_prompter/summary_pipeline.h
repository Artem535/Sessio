#pragma once

#include "prompter_pipeline.h"
#include <QDate>
#include <QJsonArray>
#include <QStringList>

namespace pcm::prompter {

struct SummaryContext {
  QDate callDate;          // meeting dates resolve against this day
  QStringList agreements;  // accepted agreements of the meeting, never repeated as tasks
};

// The meeting summary, run once after the call over the whole transcript.
// Result (format 2):
//   {format:2, overview:{text,evidence_ids}, key_points:[{text,evidence_ids}],
//    tasks:[{text,evidence_ids,meeting,date,time}], open_questions:[{text,evidence_ids}]}
// overview <=1000 UTF-16 units, <=10 key points of <=120, <=8 tasks of <=160,
// <=5 open questions of <=160; every nonempty item cites 1-8 unique sources.
//
// Each chunk of the transcript gets one leaf call: a chunk note and that
// chunk's points, tasks and questions. A rejected leaf is retried once as two
// halves and a half that fails again is left out. The lists are merged in
// call order without a model: repeats collapse, and a list over its limit
// keeps items spread over the whole call, the first and last included. The
// overview is written from the chunk notes, reduced in groups that fit one
// call; a rejected overview call falls back to the joined notes. Tasks equal
// to an accepted agreement (case, spacing and final punctuation ignored) are
// dropped. Dates come only from a calendar list built from ctx.callDate.
// Every call passes a reflect-cpp JSON schema whose evidence IDs may only name
// that call's sources. Empty input returns an empty summary without a model
// call. Throws PipelineError on invalid input, budget exhaustion, or when no
// leaf produced a usable reply; other generator exceptions propagate.
QJsonObject processSummary(const QJsonArray &utterances, const SummaryContext &ctx, const Generate &generate);

} // namespace pcm::prompter
