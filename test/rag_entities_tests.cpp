#include "entities.h"

#include <gtest/gtest.h>

namespace {

using sessio::rag::Entity;
using sessio::rag::matchEntities;

const std::vector<Entity> kEntities = {
    {"ULTRA Lean", "project", {"d1"}},
    {"Акрихин", "client", {"d2"}},
    {"Acme", "project", {"d3"}},
    {"Orion", "project", {"d4"}},
};

TEST(RagEntities, ExactNameMatch) {
  const auto matches = matchEntities("Что обсуждали на звонке с Акрихин?", kEntities);
  ASSERT_EQ(matches.size(), 1u);
  EXPECT_EQ(matches[0].name, "Акрихин");
  EXPECT_EQ(matches[0].docIds, std::vector<std::string>{"d2"});
}

TEST(RagEntities, GarbledNameFuzzyMatch) {
  // STT-style single-character corruption of a long name.
  const auto matches = matchEntities("какой статус по ULTRA Leann", kEntities);
  ASSERT_FALSE(matches.empty());
  EXPECT_EQ(matches[0].name, "ULTRA Lean");
}

TEST(RagEntities, ShortNameRequiresExactToken) {
  EXPECT_TRUE(matchEntities("проект марс закрыт", kEntities).empty());
  const auto matches = matchEntities("проект Acme закрыт", kEntities);
  ASSERT_EQ(matches.size(), 1u);
  EXPECT_EQ(matches[0].name, "Acme");
}

TEST(RagEntities, UnrelatedUtteranceHasNoMatches) {
  EXPECT_TRUE(matchEntities("завтра встреча в семнадцать ноль ноль", kEntities).empty());
}

TEST(RagEntities, CaseAndYoNormalized) {
  const auto matches = matchEntities("что по Orion?", kEntities);
  ASSERT_EQ(matches.size(), 1u);
  EXPECT_EQ(matches[0].name, "Orion");
}

} // namespace
