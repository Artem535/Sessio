#include "router_head.h"

#include "router_head_data.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace {

using sessio::rag::classifyUtterance;
using sessio::rag::headShouldSearch;
using sessio::rag::UtteranceClass;

std::vector<float> classDirection(int c) {
  std::vector<float> v(sessio::rag::router_head::kDim);
  float norm = 0;
  for (int i = 0; i < sessio::rag::router_head::kDim; ++i) {
    v[i] = sessio::rag::router_head::kWeights[c][i];
    norm += v[i] * v[i];
  }
  norm = std::sqrt(norm);
  for (auto &x : v) x /= norm;
  return v;
}

TEST(RagRouterHead, ClassDirectionsClassifyToTheirClass) {
  for (int c = 0; c < 4; ++c)
    EXPECT_EQ(static_cast<int>(classifyUtterance(classDirection(c))), c) << "class " << c;
}

TEST(RagRouterHead, IgnoreClassDoesNotSearchOthersDo) {
  EXPECT_FALSE(headShouldSearch(classDirection(3)));
  for (int c = 0; c < 3; ++c) EXPECT_TRUE(headShouldSearch(classDirection(c))) << "class " << c;
}

TEST(RagRouterHead, WrongDimensionIsIgnore) {
  EXPECT_EQ(classifyUtterance(std::vector<float>(8, 0.0f)), UtteranceClass::Ignore);
  EXPECT_FALSE(headShouldSearch(std::vector<float>(8, 0.0f)));
}

} // namespace
