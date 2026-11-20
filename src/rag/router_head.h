#pragma once

#include <vector>

namespace sessio::rag {

enum class UtteranceClass { Question = 0, Continuation = 1, Navigation = 2, Ignore = 3 };

// Router v2: linear head on the shared embedding encoder (handoff 17.11 §2).
// Trained on the draft utterance dataset; retrain on real transcripts before
// production. Wrong-sized vectors classify as Ignore (safe default).
UtteranceClass classifyUtterance(const std::vector<float> &embedding);

// Search-worthy = every class except ignore, matching the rules baseline's
// binary mapping.
bool headShouldSearch(const std::vector<float> &embedding);

} // namespace sessio::rag
