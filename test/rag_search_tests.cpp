#include "search.h"

#include "index_store.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using sessio::rag::DocumentInfo;
using sessio::rag::EmbedFn;
using sessio::rag::IndexedChunk;
using sessio::rag::IndexStore;
using sessio::rag::kEmbeddingDim;
using sessio::rag::searchWiki;

#ifndef SESSIO_LADYBUG_EXTENSION_DIR
#define SESSIO_LADYBUG_EXTENSION_DIR ""
#endif

std::vector<float> basis(int axis, float noise = 0.0f) {
  std::vector<float> vec(kEmbeddingDim, 0.0f);
  vec[axis] = 1.0f;
  if (axis + 1 < kEmbeddingDim) vec[axis + 1] = noise;
  return vec;
}

DocumentInfo doc(const std::string &id, const std::string &docType = "other") {
  DocumentInfo out;
  out.docId = id;
  out.title = "Title " + id;
  out.url = "https://example.invalid/" + id;
  out.collection = "Presale";
  out.revision = "1";
  out.docType = docType;
  return out;
}

class RagSearch : public ::testing::Test {
protected:
  void SetUp() override {
    if (std::string(SESSIO_LADYBUG_EXTENSION_DIR).empty())
      GTEST_SKIP() << "SESSIO_LADYBUG_DIR not configured";
    store = std::make_unique<IndexStore>(":memory:", SESSIO_LADYBUG_EXTENSION_DIR, nullptr);
    ASSERT_TRUE(store->ok());
    embed = [](const std::string &, std::vector<float> *out) {
      *out = basis(0);
      return true;
    };
  }
  std::unique_ptr<IndexStore> store;
  EmbedFn embed;
};

TEST_F(RagSearch, DeduplicatesMultiChunkDocumentKeepingBestChunk) {
  std::string error;
  ASSERT_TRUE(store->upsertDocument(doc("d1"),
                                    {{"отели лучший кейс", basis(0)},
                                     {"отели кейс второй", basis(0, 0.2f)},
                                     {"отели кейс третий", basis(0, 0.4f)}},
                                    &error))
      << error;
  ASSERT_TRUE(store->upsertDocument(doc("d2"), {{"ритейл магазины проект", basis(1)}}, &error)) << error;
  const auto results = searchWiki(*store, embed, "магазины отели", 10, &error);
  ASSERT_EQ(results.size(), 2u) << error;
  int d1Count = 0;
  for (const auto &r : results) {
    if (r.doc.docId == "d1") {
      ++d1Count;
      EXPECT_EQ(r.snippet, "отели лучший кейс");
      EXPECT_EQ(r.doc.url, "https://example.invalid/d1");
    }
  }
  EXPECT_EQ(d1Count, 1);
}

TEST_F(RagSearch, RespectsResultLimit) {
  std::string error;
  ASSERT_TRUE(store->upsertDocument(doc("d1"), {{"отели кейс", basis(0)}}, &error)) << error;
  ASSERT_TRUE(store->upsertDocument(doc("d2"), {{"ритейл магазины", basis(1)}}, &error)) << error;
  EXPECT_EQ(searchWiki(*store, embed, "магазины", 1, &error).size(), 1u);
}

TEST_F(RagSearch, EmbeddingFailurePropagates) {
  const EmbedFn failing = [](const std::string &, std::vector<float> *) { return false; };
  std::string error;
  EXPECT_TRUE(searchWiki(*store, failing, "запрос", 5, &error).empty());
  EXPECT_FALSE(error.empty());
}

TEST_F(RagSearch, EmptyQueryReturnsNothing) {
  std::string error;
  EXPECT_TRUE(searchWiki(*store, embed, "", 5, &error).empty());
}

TEST_F(RagSearch, FtsOnlyMatchStillFoundWhenDenseMisses) {
  std::string error;
  ASSERT_TRUE(store->upsertDocument(doc("d1"), {{"магазины в совершенно другом контексте", basis(5)}}, &error))
      << error;
  const auto results = searchWiki(*store, embed, "магазины", 10, &error);
  ASSERT_EQ(results.size(), 1u) << error;
  EXPECT_EQ(results[0].doc.docId, "d1");
}

TEST_F(RagSearch, FarDenseOnlyHitIsCutByDistanceGate) {
  std::string error;
  ASSERT_TRUE(store->upsertDocument(doc("d1"), {{"совершенно другой текст", basis(5)}}, &error)) << error;
  EXPECT_TRUE(searchWiki(*store, embed, "магазины", 10, &error).empty());
}

TEST_F(RagSearch, ResolvePassportsMatchesDescriptionAndCutsFar) {
  sessio::rag::Passport near, far;
  near.name = "Acme";
  near.docId = "d1";
  near.url = "https://example.invalid/d1";
  near.description = "производитель удобрений";
  near.embedding = basis(0);
  far.name = "Other";
  far.embedding = basis(5);
  std::string error;
  const auto hits = sessio::rag::resolvePassports(embed, {near, far}, "производитель удобрений", 2, &error);
  ASSERT_EQ(hits.size(), 1u) << error;
  EXPECT_EQ(hits[0].name, "Acme");
  EXPECT_EQ(hits[0].docId, "d1");
}

TEST_F(RagSearch, ResolvePassportsKeepsOnlyCloseRunnerUp) {
  sessio::rag::Passport best, close, distant;
  best.name = "Best";
  best.embedding = basis(0);
  close.name = "Close";
  close.embedding = basis(0, 0.01f);
  distant.name = "Distant";
  distant.embedding = basis(0, 0.5f);
  std::string error;
  const auto hits = sessio::rag::resolvePassports(embed, {best, close, distant}, "описание", 3, &error);
  ASSERT_EQ(hits.size(), 2u) << error;
  EXPECT_EQ(hits[0].name, "Best");
  EXPECT_EQ(hits[1].name, "Close");
}

TEST_F(RagSearch, EntityBoostPrefersEntityDocument) {
  std::string error;
  ASSERT_TRUE(store->upsertDocument(doc("d1"), {{"отели кейс внедрения", basis(0)}}, &error)) << error;
  ASSERT_TRUE(store->upsertDocument(doc("d2"), {{"отели кейс второй", basis(0, 0.05f)}}, &error)) << error;
  const auto plain = searchWiki(*store, embed, "отели кейс", 1, &error);
  ASSERT_EQ(plain.size(), 1u);
  EXPECT_EQ(plain[0].doc.docId, "d1");
  const auto boosted = searchWikiBoosted(*store, embed, {"отели кейс"}, {"d2"}, "", 1, &error);
  ASSERT_EQ(boosted.size(), 1u);
  EXPECT_EQ(boosted[0].doc.docId, "d2");
}

TEST_F(RagSearch, CategoryBoostIsCappedByRelevance) {
  std::string error;
  // d1 matches both rankers; d2 only matches dense and is a case page.
  ASSERT_TRUE(store->upsertDocument(doc("d1", "prd"), {{"отели гостиничные объекты", basis(0)}}, &error))
      << error;
  ASSERT_TRUE(store->upsertDocument(doc("d2", "case"), {{"общий обзор кейсов", basis(0, 0.2f)}}, &error))
      << error;
  const auto results = searchWikiBoosted(*store, embed, {"отели"}, {}, "cases", 2, &error);
  ASSERT_EQ(results.size(), 2u) << error;
  // A flat +3/61 boost used to put the weak case page first; the cap keeps the
  // strongly matching prd on top.
  EXPECT_EQ(results[0].doc.docId, "d1");
  EXPECT_EQ(results[1].doc.docId, "d2");
}

TEST_F(RagSearch, GenericCaseDeckTitleIsDownranked) {
  std::string error;
  auto generic = doc("d1", "case");
  generic.title = "Кейсы по кастдеву";
  auto specific = doc("d2", "case");
  specific.title = "Acme Med Co-pilot";
  ASSERT_TRUE(store->upsertDocument(generic, {{"кейсы внедрения отели", basis(0)}}, &error)) << error;
  ASSERT_TRUE(store->upsertDocument(specific, {{"кейсы внедрения отели", basis(0)}}, &error)) << error;
  const auto results = searchWikiBoosted(*store, embed, {"отели кейс"}, {}, "cases", 2, &error);
  ASSERT_EQ(results.size(), 2u) << error;
  EXPECT_EQ(results[0].doc.docId, "d2"); // concrete project page
  EXPECT_EQ(results[1].doc.docId, "d1"); // generic deck
}

TEST_F(RagSearch, EntityTableRoundtrip) {
  std::string error;
  const std::vector<sessio::rag::Entity> entities = {{"ULTRA Lean", "project", {"d1", "d2"}}};
  ASSERT_TRUE(store->upsertEntities(entities, &error)) << error;
  ASSERT_TRUE(store->upsertEntities({{"ULTRA Lean", "project", {"d3"}}}, &error)) << error;
  const auto all = store->allEntities();
  ASSERT_EQ(all.size(), 1u);
  EXPECT_EQ(all[0].name, "ULTRA Lean");
  EXPECT_EQ(all[0].docIds, (std::vector<std::string>{"d1", "d2", "d3"}));
}

TEST(RagCardSnippet, ShortMarkdownUnchanged) {
  EXPECT_EQ(sessio::rag::cardSnippet("**bold** text"), "**bold** text");
}

TEST(RagCardSnippet, DropsMarkdownNoise) {
  EXPECT_EQ(sessio::rag::cardSnippet("---"), "");
  EXPECT_EQ(sessio::rag::cardSnippet("\\"), "");
  EXPECT_EQ(sessio::rag::cardSnippet("---\n\nНастоящий текст про проект"), "Настоящий текст про проект");
  EXPECT_EQ(sessio::rag::cardSnippet("```\ncode\n```\n\nТекст про проект"), "Текст про проект");
  EXPECT_EQ(sessio::rag::cardSnippet("| --- | --- |\n| a | b |"), "| a | b |");
}

TEST(RagCardSnippet, CutsAtParagraphBoundary) {
  const std::string text = std::string(300, 'a') + "\n\n" + std::string(500, 'b');
  const auto cut = sessio::rag::cardSnippet(text, 600);
  EXPECT_EQ(cut, std::string(300, 'a') + "…");
}

TEST(RagCardSnippet, CutsAtWordBoundaryWithoutParagraphs) {
  const std::string text = std::string(700, 'a') + " tail";
  const auto cut = sessio::rag::cardSnippet(text, 600);
  EXPECT_EQ(cut, std::string(600, 'a') + "…");
}

TEST(RagCardSnippet, CountsCodePointsNotBytes) {
  std::string text, expected;
  for (int i = 0; i < 700; ++i) text += "я";
  for (int i = 0; i < 600; ++i) expected += "я";
  expected += "…";
  EXPECT_EQ(sessio::rag::cardSnippet(text, 600), expected);
}

} // namespace
