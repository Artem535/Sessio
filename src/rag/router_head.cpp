#include "router_head.h"

#include "router_head_data.h"

#include <cmath>

namespace sessio::rag {

UtteranceClass classifyUtterance(const std::vector<float> &embedding) {
  if (embedding.size() != static_cast<size_t>(router_head::kDim)) return UtteranceClass::Ignore;
  int best = 0;
  float bestScore = 0;
  for (int c = 0; c < router_head::kClasses; ++c) {
    float score = router_head::kBias[c];
    for (int i = 0; i < router_head::kDim; ++i)
      score += router_head::kWeights[c][i] * embedding[i];
    if (c == 0 || score > bestScore) {
      best = c;
      bestScore = score;
    }
  }
  return static_cast<UtteranceClass>(best);
}

bool headShouldSearch(const std::vector<float> &embedding) {
  return classifyUtterance(embedding) != UtteranceClass::Ignore;
}

} // namespace sessio::rag
