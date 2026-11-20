#pragma once

#include <string_view>

namespace sessio::rag {

// Router v1: deterministic rules, no model. True when the utterance looks
// worth searching the wiki for: a "?" anywhere, a Russian question word, or
// a request verb ("расскажите", "подскажите", ...) as a whole token
// (case-insensitive). Continuation questions ("А такое у вас было?") count
// as search-worthy; only pure chatter is skipped. Known limitation: live
// ASR transcripts may lack punctuation; a trained embedding head replaces
// these rules once real transcripts accumulate (handoff 17.11 §2).
bool shouldSearch(std::string_view utterance);

} // namespace sessio::rag
