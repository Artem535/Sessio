#include "index_store.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

using sessio::rag::DocumentInfo;
using sessio::rag::IndexedChunk;
using sessio::rag::IndexStore;
using sessio::rag::kEmbeddingDim;

#ifndef SESSIO_LADYBUG_EXTENSION_DIR
#define SESSIO_LADYBUG_EXTENSION_DIR ""
#endif

std::vector<float> basis(int axis, float scale = 1.0f) {
  std::vector<float> vec(kEmbeddingDim, 0.0f);
  vec[axis] = scale;
  return vec;
}

DocumentInfo doc(const std::string &id, const std::string &revision) {
  DocumentInfo out;
  out.docId = id;
  out.title = "Title " + id;
  out.url = "https://example.invalid/" + id;
  out.collection = "Presale";
  out.revision = revision;
  return out;
}

std::string extensionDir() { return SESSIO_LADYBUG_EXTENSION_DIR; }

class RagIndexStore : public ::testing::Test {
protected:
  void SetUp() override {
    if (extensionDir().empty()) GTEST_SKIP() << "SESSIO_LADYBUG_DIR not configured";
  }
  std::string dbPath(const std::string &name) {
    const auto dir = std::filesystem::path(::testing::TempDir()) / ("rag_index_" + name);
    std::filesystem::remove_all(dir);
    return dir.string();
  }
};

TEST_F(RagIndexStore, PassportRoundtripAndUpsertByName) {
  IndexStore store(":memory:", extensionDir(), nullptr);
  ASSERT_TRUE(store.ok());
  sessio::rag::Passport passport;
  passport.name = "Acme";
  passport.industry = "производитель удобрений";
  passport.clientType = "производитель";
  passport.whatBuilt = "суфлёр встреч";
  passport.deployment = "on-prem";
  passport.description = "производитель удобрений, он-прем развёртывание моделей";
  passport.docId = "d1";
  passport.url = "https://example.invalid/d1";
  passport.aliases = {"Акме", "Acme Crop"};
  passport.models = {"lfm3"};
  passport.embedding = basis(0);
  std::string error;
  ASSERT_TRUE(store.upsertPassports({passport}, &error)) << error;
  auto all = store.allPassports();
  ASSERT_EQ(all.size(), 1u);
  EXPECT_EQ(all[0].name, "Acme");
  EXPECT_EQ(all[0].industry, "производитель удобрений");
  EXPECT_EQ(all[0].deployment, "on-prem");
  EXPECT_EQ(all[0].docId, "d1");
  EXPECT_EQ(all[0].aliases, (std::vector<std::string>{"Акме", "Acme Crop"}));
  EXPECT_EQ(all[0].models, (std::vector<std::string>{"lfm3"}));
  ASSERT_EQ(all[0].embedding.size(), static_cast<size_t>(kEmbeddingDim));
  EXPECT_FLOAT_EQ(all[0].embedding[0], 1.0f);
  passport.deployment = "cloud";
  ASSERT_TRUE(store.upsertPassports({passport}, &error)) << error;
  all = store.allPassports();
  ASSERT_EQ(all.size(), 1u);
  EXPECT_EQ(all[0].deployment, "cloud");
}

TEST_F(RagIndexStore, FtsFindsChunkWithRussianStemming) {
  IndexStore store(":memory:", extensionDir(), nullptr);
  ASSERT_TRUE(store.ok());
  std::string error;
  ASSERT_TRUE(store.upsertDocument(doc("d1", "2"),
                                   {{"Проект для розничной торговой сети магазинов", basis(0)}}, &error))
      << error;
  const auto hits = store.searchFts("магазин", 10, &error);
  ASSERT_EQ(hits.size(), 1u) << error;
  EXPECT_EQ(hits[0].docId, "d1");
  EXPECT_GT(hits[0].score, 0.0);
}

TEST_F(RagIndexStore, InvalidUtf8FtsQueryDoesNotAbort) {
  IndexStore store(":memory:", extensionDir(), nullptr);
  ASSERT_TRUE(store.ok());
  std::string error;
  ASSERT_TRUE(store.upsertDocument(doc("d1", "1"), {{"текст про магазины", basis(0)}}, &error)) << error;
  // ASR/LLM text can carry a lone or truncated byte; lbug rejects non-UTF-8
  // outright, so the query must be sanitized instead of aborting the search.
  const std::string broken = std::string("магазины \xD0");
  EXPECT_NO_THROW({
    const auto hits = store.searchFts(broken, 5, &error);
    EXPECT_EQ(hits.size(), 1u);
  });
  EXPECT_EQ(store.searchFts("магазины", 5, &error).size(), 1u);
}

TEST_F(RagIndexStore, VectorSearchOrdersByCosineDistance) {
  IndexStore store(":memory:", extensionDir(), nullptr);
  ASSERT_TRUE(store.ok());
  std::string error;
  ASSERT_TRUE(store.upsertDocument(doc("d1", "1"),
                                   {{"near", basis(0)}, {"far", basis(1)}}, &error))
      << error;
  auto query = basis(0);
  query[1] = 0.1f;
  const auto hits = store.searchVector(query, 2, &error);
  ASSERT_EQ(hits.size(), 2u) << error;
  EXPECT_EQ(hits[0].chunkId, 0); // SERIAL ids start at 0, insertion order
  EXPECT_LT(hits[0].score, hits[1].score);
}

TEST_F(RagIndexStore, UpsertReplacesChunksAndUpdatesRevision) {
  IndexStore store(":memory:", extensionDir(), nullptr);
  ASSERT_TRUE(store.ok());
  std::string error;
  ASSERT_TRUE(store.upsertDocument(doc("d1", "2"),
                                   {{"старый текст про магазины", basis(0)}, {"ещё старый", basis(1)}}, &error))
      << error;
  ASSERT_TRUE(store.upsertDocument(doc("d1", "3"), {{"новый текст про отели", basis(2)}}, &error)) << error;
  EXPECT_EQ(store.documentRevision("d1"), 3);
  EXPECT_TRUE(store.searchFts("магазины", 10, &error).empty());
  const auto hits = store.searchFts("отели", 10, &error);
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].docId, "d1");
}

TEST_F(RagIndexStore, RemoveDocumentClearsChunksAndMeta) {
  IndexStore store(":memory:", extensionDir(), nullptr);
  ASSERT_TRUE(store.ok());
  std::string error;
  ASSERT_TRUE(store.upsertDocument(doc("d1", "1"), {{"текст про магазины", basis(0)}}, &error)) << error;
  ASSERT_TRUE(store.removeDocument("d1", &error)) << error;
  EXPECT_FALSE(store.documentRevision("d1").has_value());
  EXPECT_TRUE(store.searchFts("магазины", 10, &error).empty());
  EXPECT_TRUE(store.searchVector(basis(0), 10, &error).empty());
}

TEST_F(RagIndexStore, FileDatabasePersistsAcrossReopen) {
  const auto path = dbPath("persist");
  {
    IndexStore store(path, extensionDir(), nullptr);
    ASSERT_TRUE(store.ok());
    std::string error;
    ASSERT_TRUE(store.upsertDocument(doc("d1", "5"), {{"текст про магазины", basis(0)}}, &error)) << error;
  }
  IndexStore reopened(path, extensionDir(), nullptr);
  ASSERT_TRUE(reopened.ok());
  EXPECT_EQ(reopened.documentRevision("d1"), 5);
  std::string error;
  EXPECT_EQ(reopened.searchFts("магазины", 10, &error).size(), 1u);
  std::filesystem::remove_all(path);
}

TEST_F(RagIndexStore, RejectsWrongEmbeddingDimension) {
  IndexStore store(":memory:", extensionDir(), nullptr);
  ASSERT_TRUE(store.ok());
  std::string error;
  EXPECT_FALSE(store.upsertDocument(doc("d1", "1"), {{"text", std::vector<float>(8, 0.0f)}}, &error));
  EXPECT_FALSE(error.empty());
  EXPECT_TRUE(store.searchVector(std::vector<float>(8, 0.0f), 5, &error).empty());
}

} // namespace
