#pragma once

#include "chunk.h"
#include "index_store.h"

#include <functional>
#include <string>
#include <vector>

namespace sessio::rag {

struct SearchResult {
  DocumentInfo doc;
  std::string snippet; // text of the document's best-ranked chunk
  double score = 0;    // RRF score, higher is better
};

struct PassportHit {
  std::string name;
  std::string docId;
  std::string url;
  std::string description;
  double distance = 0; // cosine distance, lower is better
};

// Embeds one already-prefixed text into kEmbeddingDim floats; false on failure.
using EmbedFn = std::function<bool(const std::string &, std::vector<float> *)>;

// Hybrid retrieval: dense cosine + FTS BM25, reciprocal rank fusion (k=60),
// then document-level dedup keeping each document's best chunk. The query
// recipe prefix is applied here; the embedder itself stays recipe-agnostic
// so the same encoder can serve the router head later.
std::vector<SearchResult> searchWiki(IndexStore &index, const EmbedFn &embed, const std::string &query,
                                     int k, std::string *error);

// Noise-robust retrieval for live ASR: fuse several phrasings of the same
// intent (current utterance, stripped utterance, previous cleaner phrase)
// with RRF before document dedup. A garbled current phrase is compensated
// by a cleaner variant instead of relying on one noisy query.
std::vector<SearchResult> searchWikiVariants(IndexStore &index, const EmbedFn &embed,
                                             const std::vector<std::string> &queries, int k,
                                             std::string *error);

// Same, with a document-level bonus for docs carrying recognized entities
// (primary only: the document is ABOUT the entity, not merely mentions it)
// and for docs whose type matches the question category. Results below the
// relevance cutoff are dropped (no-data guard against garbage cards).
std::vector<SearchResult> searchWikiBoosted(IndexStore &index, const EmbedFn &embed,
                                            const std::vector<std::string> &queries,
                                            const std::vector<std::string> &boostDocIds,
                                            const std::string &category, int k, std::string *error);

// Resolves a spoken project description ("производитель удобрений") against
// the passport collection by cosine distance of embeddings. A second passport
// is returned only when its distance is close to the best one (the speaker
// themselves was unsure: "вернее даже два"); distant runners-up are noise.
std::vector<PassportHit> resolvePassports(const EmbedFn &embed, const std::vector<Passport> &passports,
                                          const std::string &description, int k, std::string *error);

// Card-sized markdown excerpt: keeps whole paragraphs when possible, cuts at
// a word boundary otherwise, counts code points (Cyrillic-safe).
std::string cardSnippet(const std::string &markdown, size_t limit = 600);

} // namespace sessio::rag
