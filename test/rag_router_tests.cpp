#include "router.h"

#include <gtest/gtest.h>

#include <fstream>
#include <string>

namespace {

using sessio::rag::shouldSearch;

TEST(RagRouter, QuestionMarkTriggersSearch) {
  EXPECT_TRUE(shouldSearch("Расскажите про интеграцию?"));
}

TEST(RagRouter, QuestionWordWithoutMarkTriggersSearch) {
  EXPECT_TRUE(shouldSearch("Где лежит описание кейса"));
  EXPECT_TRUE(shouldSearch("сколько этапов внедрения"));
}

TEST(RagRouter, QuestionWordMatchIsCaseInsensitive) {
  EXPECT_TRUE(shouldSearch("КАКОЙ статус у пилота"));
  EXPECT_TRUE(shouldSearch("Чьё это решение"));
}

TEST(RagRouter, QuestionWordInsideAnotherWordDoesNotTrigger) {
  EXPECT_FALSE(shouldSearch("Никакой статус не менялся"));
  EXPECT_FALSE(shouldSearch("Мы сделали это обычным способом"));
}

TEST(RagRouter, DeclarativeWithoutQuestionWordsSkipped) {
  EXPECT_FALSE(shouldSearch("Да, нам это интересно, давайте обсудим сроки."));
  EXPECT_FALSE(shouldSearch("Мы пока просто присматриваемся."));
}

TEST(RagRouter, ContinuationQuestionTriggersSearch) {
  // Echo questions still ask about company experience — worth a wiki lookup.
  EXPECT_TRUE(shouldSearch("А с нейросетями по-новее пробовали?"));
  EXPECT_TRUE(shouldSearch("А такое у вас было?"));
}

TEST(RagRouter, RequestVerbTriggersSearchWithoutQuestionMark) {
  EXPECT_TRUE(shouldSearch("Расскажите про самый большой ваш проект"));
  EXPECT_TRUE(shouldSearch("Подскажите статус пилота"));
}

TEST(RagRouter, EmptyUtteranceSkipped) {
  EXPECT_FALSE(shouldSearch(""));
  EXPECT_FALSE(shouldSearch("   "));
}

TEST(RagRouter, DraftDatasetRecallAndPrecisionMatchRulesBaseline) {
  // Anonymized copy of router_ds_draft.jsonl (84 synthetic utterances,
  // handoff 17.11 §2). Search-worthy = everything except ignore, matching
  // the binary mapping of FRIDA-BENCH-REPORT-2026-11-16 (recall 1.00,
  // precision 0.92 for the rules baseline).
  std::ifstream in(std::string(SESSIO_SOURCE_DIR) + "/test/resources/rag/router_ds.tsv");
  ASSERT_TRUE(in.is_open());
  int tp = 0, fp = 0, fn = 0, total = 0;
  std::string line;
  while (std::getline(in, line)) {
    const auto tab = line.find('\t');
    ASSERT_NE(tab, std::string::npos);
    const std::string label = line.substr(0, tab);
    const std::string text = line.substr(tab + 1);
    const bool positive = label != "ignore";
    const bool predicted = shouldSearch(text);
    if (positive && predicted) ++tp;
    if (!positive && predicted) ++fp;
    if (positive && !predicted) {
      ++fn;
      ADD_FAILURE() << "missed search-worthy utterance: " << text;
    }
    ++total;
  }
  EXPECT_EQ(total, 84);
  EXPECT_EQ(fn, 0);
  const double precision = tp + fp > 0 ? static_cast<double>(tp) / (tp + fp) : 0.0;
  EXPECT_GE(precision, 0.90) << "tp=" << tp << " fp=" << fp;
}

} // namespace
