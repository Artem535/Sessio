#pragma once

#include "chunk.h"
#include "entities.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sessio::rag {

inline constexpr int kEmbeddingDim = 768;

struct SearchHit {
  int64_t chunkId = 0;
  std::string docId;
  std::string text;
  double score = 0; // fts: BM25 (higher is better); vector: cosine distance (lower is better)
};

struct IndexedChunk {
  std::string text;
  std::vector<float> embedding; // kEmbeddingDim entries
};

struct FaqEntry {
  std::string docId;
  std::string question;
  std::string answer;
  std::string url;
  std::vector<float> embedding; // of the question
};

struct FaqHit {
  std::string question;
  std::string answer;
  std::string url;
  std::string docId;
  double distance = 0; // cosine distance, lower is better
};

// Project passport: a curated description collection entry. The description
// embedding is matched against spoken project descriptions ("производитель
// удобрений") when the utterance carries no project name.
struct Passport {
  std::string name;
  std::string industry;
  std::string clientType;
  std::string whatBuilt;
  std::string deployment; // on-prem | cloud | mixed | unknown
  std::string description;
  std::string docId; // primary project page
  std::string url;
  std::vector<std::string> aliases;
  std::vector<std::string> models;
  std::vector<float> embedding; // of description, kEmbeddingDim entries
};

// Embedded LadybugDB index: Document/Chunk tables, Russian-stemmed FTS and
// cosine vector indexes. One writer at a time (the RagWorker thread owns the
// store). Extensions load from local files only — no network at runtime.
class IndexStore final {
public:
  // dbPath may be ":memory:"; extensionDir holds fts/ and vector/ subdirs.
  IndexStore(const std::string &dbPath, const std::string &extensionDir, std::string *error);
  ~IndexStore();
  IndexStore(const IndexStore &) = delete;
  IndexStore &operator=(const IndexStore &) = delete;

  bool ok() const;
  // Replaces every chunk of the document and records its revision.
  bool upsertDocument(const DocumentInfo &doc, const std::vector<IndexedChunk> &chunks, std::string *error);
  bool removeDocument(const std::string &docId, std::string *error);
  std::optional<int64_t> documentRevision(const std::string &docId);
  std::optional<DocumentInfo> documentInfo(const std::string &docId);
  // Named-entity dictionary (NER over the wiki) for fuzzy query repair.
  bool upsertEntities(const std::vector<Entity> &entities, std::string *error);
  std::vector<Entity> allEntities();
  bool upsertFaq(const std::vector<FaqEntry> &entries, std::string *error);
  std::vector<FaqHit> searchFaq(const std::vector<float> &embedding, int k, std::string *error);
  bool upsertPassports(const std::vector<Passport> &passports, std::string *error);
  std::vector<Passport> allPassports();
  std::vector<SearchHit> searchFts(const std::string &query, int k, std::string *error);
  std::vector<SearchHit> searchVector(const std::vector<float> &embedding, int k, std::string *error);

private:
  struct Impl;
  std::unique_ptr<Impl> mImpl;
};

} // namespace sessio::rag
