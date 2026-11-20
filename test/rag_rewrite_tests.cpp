#include "rewrite.h"

#include <gtest/gtest.h>

namespace {

using sessio::rag::classifyCategory;
using sessio::rag::rewriteQuery;

TEST(RagRewrite, StripsLeadingConjunction) {
  EXPECT_EQ(rewriteQuery("А сколько это стоило?", ""), "сколько это стоило?");
}

TEST(RagRewrite, AnaphoricUtteranceGainsTopic) {
  EXPECT_EQ(rewriteQuery("А такое у вас было?", "интеграция с CRM"),
            "такое у вас было? интеграция с CRM");
}

TEST(RagRewrite, SelfContainedQueryUnchanged) {
  EXPECT_EQ(rewriteQuery("Где лежит PRD по отелям?", "интеграция с CRM"),
            "Где лежит PRD по отелям?");
}

TEST(RagRewrite, AnaphoraWithoutContextUnchanged) {
  EXPECT_EQ(rewriteQuery("А это в проде крутится?", ""), "это в проде крутится?");
}

TEST(RagRewrite, EmptyUtteranceStaysEmpty) {
  EXPECT_EQ(rewriteQuery("   ", "тема"), "");
}

TEST(RagRewrite, NavigationWinsOverIncidentalProjectWord) {
  // "проект" must not turn a "where is it stored" question into a case query.
  EXPECT_EQ(classifyCategory("Где у нас лежат КП и оценки по проектам?"), "navigation");
  EXPECT_EQ(classifyCategory("Где найти договоры по AI-направлению?"), "navigation");
  EXPECT_EQ(classifyCategory("В какой папке инструменты для пресейла?"), "navigation");
}

TEST(RagRewrite, IndustryProjectQuestionStaysCases) {
  // No storage cue: these are still "which projects" questions.
  EXPECT_EQ(classifyCategory("Какие проекты у вас были в медицине?"), "cases");
  EXPECT_EQ(classifyCategory("Где вы делали проекты в медицине?"), "cases");
}

} // namespace
