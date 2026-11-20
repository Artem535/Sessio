#pragma once

#include <string>
#include <string_view>

namespace sessio::rag {

// Query rewrite v1: deterministic context substitution, no model. Strips the
// leading continuation conjunction and, when the utterance is anaphoric
// ("это", "такой", "у вас", ...), appends the dialogue topic so the query is
// self-contained for retrieval. A model-based rewrite on the Gemma engine is
// a later step (handoff 17.11 §8); the engine is single-request.
std::string rewriteQuery(std::string_view utterance, std::string_view context);

// Question category for doc-type boosting: cases | status | navigation |
// pricing | integration | other. Rules fallback for the LLM classifier.
std::string classifyCategory(std::string_view utterance);

} // namespace sessio::rag
