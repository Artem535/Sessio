#include "search.h"

#include "index_store.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <unordered_map>

namespace sessio::rag {

namespace {

constexpr int kFetch = 50;
constexpr int kRrfK = 60;
// Noise gate on the dense cosine distance; a lexical (FTS) match bypasses it.
// RRF score itself cannot gate: a single-variant rank-1 hit is only 1/(k+1).
constexpr double kMaxDenseDistance = 0.5;
// An FTS-only hit (no dense support at all) is weaker evidence than a hit both
// rankers agree on; downrank it instead of dropping it, so an exact lexical
// match still surfaces when nothing better exists.
constexpr double kFtsOnlyWeight = 0.6;
// A flat category boost would let a weak case page (base ~0.02) leapfrog a
// strongly relevant non-case page (base ~0.05). Cap the boost at a fraction of
// the document's own base score: relevance still matters, doc_type only tips
// near-ties.
constexpr double kBoostBaseFactor = 0.8;
// Internal working documents (questionnaires, checklists, summaries, backlogs)
// and generic case decks ("кейсы по кастдеву") are not answer cards; downrank
// them so they stop crowding out concrete project pages.
constexpr double kBoilerplateWeight = 0.5;
// Calibrated on the resolver gate (spikes/rag-resolver-gate): client-case
// descriptions land at 0.11-0.22, absent industries at 0.25+.
constexpr double kMaxPassportDistance = 0.25;
// A runner-up passport joins the best one only within this distance delta.
constexpr double kPassportAmbiguityDelta = 0.05;

// Lowercases ASCII and Cyrillic (А-Я, Ё); other bytes pass through.
std::string lowerCyrillic(std::string_view s) {
  std::string out(s);
  for (size_t i = 0; i < out.size(); ++i) {
    const unsigned char c = out[i];
    if (c >= 'A' && c <= 'Z') {
      out[i] = static_cast<char>(c + ('a' - 'A'));
    } else if (c == 0xD0 && i + 1 < out.size()) {
      const unsigned char next = out[i + 1];
      if (next >= 0x90 && next <= 0x9F) out[i + 1] = static_cast<char>(next + 0x20);
      else if (next >= 0xA0 && next <= 0xAF) {
        out[i] = static_cast<char>(0xD1);
        out[i + 1] = static_cast<char>(next - 0x20);
      } else if (next == 0x81) {
        out[i] = static_cast<char>(0xD1);
        out[i + 1] = static_cast<char>(0x91);
      }
      ++i;
    }
  }
  return out;
}

bool boilerplateTitle(const std::string &title) {
  const auto lowered = lowerCyrillic(title);
  static constexpr std::array<std::string_view, 14> patterns = {
      "опросник", "чеклист", "вопросы", "summary", "бэклог", "протокол", "задачи", "шаблон",
      "кейсы по", "кейс стади", "кейсстади", "презентация кейсов", "обзор кейсов", "кастдев"};
  for (const auto pattern : patterns)
    if (lowered.find(pattern) != std::string::npos) return true;
  return false;
}

std::string_view trimView(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
    text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
    text.remove_suffix(1);
  return text;
}

bool meaningfulText(std::string_view text) {
  for (const unsigned char c : text)
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c >= 0x80)
      return true;
  return false;
}

// A line made only of rule/table/heading punctuation ("---", "| --- |", "===").
bool noiseLine(std::string_view line) {
  bool any = false;
  for (const char c : line) {
    if (c == ' ' || c == '\t') continue;
    any = true;
    if (c != '-' && c != '*' && c != '_' && c != '=' && c != '|' && c != ':') return false;
  }
  return any;
}

// Skips leading blank lines, rules, table separators, fences and bare
// punctuation so a card never shows "\" or "---" as its snippet.
std::string stripMarkdownLead(const std::string &markdown) {
  size_t start = 0;
  bool inFence = false;
  while (start < markdown.size()) {
    const size_t end = markdown.find('\n', start);
    std::string_view line(markdown.data() + start, (end == std::string::npos ? markdown.size() : end) - start);
    start = end == std::string::npos ? markdown.size() : end + 1;
    auto trimmed = trimView(line);
    if (trimmed.starts_with("```")) {
      inFence = !inFence;
      continue;
    }
    if (inFence) continue;
    if (trimmed.empty() || noiseLine(trimmed)) continue;
    while (!trimmed.empty() && (trimmed.front() == '#' || trimmed.front() == '>'))
      trimmed.remove_prefix(1);
    trimmed = trimView(trimmed);
    if (trimmed.empty() || noiseLine(trimmed) || !meaningfulText(trimmed)) continue;
    return markdown.substr(static_cast<size_t>(trimmed.data() - markdown.data()));
  }
  return {};
}

double cosineDistance(const std::vector<float> &a, const std::vector<float> &b) {
  double dot = 0, na = 0, nb = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    dot += static_cast<double>(a[i]) * b[i];
    na += static_cast<double>(a[i]) * a[i];
    nb += static_cast<double>(b[i]) * b[i];
  }
  if (na <= 0 || nb <= 0) return 2;
  return 1.0 - dot / (std::sqrt(na) * std::sqrt(nb));
}

bool categoryMatches(const std::string &category, const std::string &docType) {
  if (category == "cases") return docType == "case";
  if (category == "status") return docType == "prd" || docType == "kp" || docType == "meeting";
  if (category == "pricing") return docType == "kp";
  if (category == "integration") return docType == "prd" || docType == "process";
  return false;
}

struct ChunkScore {
  std::string docId;
  std::string text;
  double score = 0;
  bool fts = false;
  double denseDistance = 2;
};

void fuse(std::unordered_map<int64_t, ChunkScore> &scores, const std::vector<SearchHit> &hits, bool isFts) {
  for (size_t rank = 0; rank < hits.size(); ++rank) {
    auto &entry = scores[hits[rank].chunkId];
    entry.docId = hits[rank].docId;
    entry.text = hits[rank].text;
    entry.score += 1.0 / (kRrfK + static_cast<double>(rank + 1));
    if (isFts) entry.fts = true;
    else entry.denseDistance = std::min(entry.denseDistance, hits[rank].score);
  }
}

} // namespace

std::vector<SearchResult> searchWiki(IndexStore &index, const EmbedFn &embed, const std::string &query,
                                     int k, std::string *error) {
  return searchWikiVariants(index, embed, {query}, k, error);
}

std::vector<SearchResult> searchWikiVariants(IndexStore &index, const EmbedFn &embed,
                                             const std::vector<std::string> &queries, int k,
                                             std::string *error) {
  return searchWikiBoosted(index, embed, queries, {}, {}, k, error);
}

std::vector<SearchResult> searchWikiBoosted(IndexStore &index, const EmbedFn &embed,
                                            const std::vector<std::string> &queries,
                                            const std::vector<std::string> &boostDocIds,
                                            const std::string &category, int k, std::string *error) {
  if (k <= 0) return {};
  std::unordered_map<int64_t, ChunkScore> scores;
  bool any = false;
  for (const auto &query : queries) {
    if (query.empty()) continue;
    std::vector<float> vector;
    if (!embed("task: search result | query: " + query, &vector)) {
      if (error) *error = "query embedding failed";
      return {};
    }
    const auto dense = index.searchVector(vector, kFetch, error);
    const auto fts = index.searchFts(query, kFetch, error);
    fuse(scores, dense, false);
    fuse(scores, fts, true);
    any = true;
  }
  if (!any) return {};

  std::unordered_map<std::string, const ChunkScore *> bestPerDoc;
  for (const auto &[chunkId, chunk] : scores) {
    auto it = bestPerDoc.find(chunk.docId);
    if (it == bestPerDoc.end() || chunk.score > it->second->score) bestPerDoc[chunk.docId] = &chunk;
  }
  std::vector<const ChunkScore *> ranked;
  ranked.reserve(bestPerDoc.size());
  for (const auto &[docId, chunk] : bestPerDoc) ranked.push_back(chunk);
  const double bonus = 1.0 / (kRrfK + 1);
  std::unordered_map<const ChunkScore *, double> boosted;
  std::unordered_map<std::string, DocumentInfo> infos;
  // Document info is needed for the doc_type boost and the boilerplate check;
  // cache it per doc so a multi-chunk candidate costs one query.
  const auto infoFor = [&](const std::string &docId) -> const DocumentInfo * {
    auto it = infos.find(docId);
    if (it == infos.end()) {
      auto info = index.documentInfo(docId);
      if (!info) return nullptr;
      it = infos.emplace(docId, std::move(*info)).first;
    }
    return &it->second;
  };
  for (const auto *chunk : ranked) {
    double score = chunk->score;
    if (chunk->fts && chunk->denseDistance >= 2) score *= kFtsOnlyWeight;
    const DocumentInfo *info = infoFor(chunk->docId);
    if (info && boilerplateTitle(info->title)) score *= kBoilerplateWeight;
    if (std::find(boostDocIds.begin(), boostDocIds.end(), chunk->docId) != boostDocIds.end())
      score += bonus;
    if (!category.empty() && info) {
      // Industry/"which projects" questions must surface case pages, not the
      // generic PRD/process pages that lexically match "проект". The boost is
      // capped by the document's own base so it only tips near-ties.
      if (categoryMatches(category, info->docType)) {
        const double raw = (category == "cases" ? 3 * bonus : bonus);
        score += std::min(raw, kBoostBaseFactor * score);
      }
    }
    boosted[chunk] = score;
  }
  std::sort(ranked.begin(), ranked.end(), [&](const ChunkScore *a, const ChunkScore *b) {
    return boosted[a] > boosted[b];
  });

  std::vector<SearchResult> results;
  for (const auto *chunk : ranked) {
    if (static_cast<int>(results.size()) >= k) break;
    if (!chunk->fts && chunk->denseDistance > kMaxDenseDistance) continue;
    const DocumentInfo *info = infoFor(chunk->docId);
    if (!info) continue;
    results.push_back(SearchResult{*info, chunk->text, boosted[chunk]});
  }
  return results;
}

std::vector<PassportHit> resolvePassports(const EmbedFn &embed, const std::vector<Passport> &passports,
                                          const std::string &description, int k, std::string *error) {
  if (k <= 0 || description.empty() || passports.empty()) return {};
  std::vector<float> vector;
  if (!embed("task: search result | query: " + description, &vector)) {
    if (error) *error = "description embedding failed";
    return {};
  }
  std::vector<PassportHit> hits;
  for (const auto &passport : passports) {
    if (passport.embedding.size() != vector.size()) continue;
    const double distance = cosineDistance(vector, passport.embedding);
    if (distance > kMaxPassportDistance) continue;
    hits.push_back({passport.name, passport.docId, passport.url, passport.description, distance});
  }
  std::sort(hits.begin(), hits.end(),
            [](const PassportHit &a, const PassportHit &b) { return a.distance < b.distance; });
  if (hits.size() > 1) {
    const double best = hits.front().distance;
    hits.erase(std::remove_if(hits.begin() + 1, hits.end(),
                              [best](const PassportHit &h) { return h.distance - best > kPassportAmbiguityDelta; }),
               hits.end());
  }
  if (static_cast<int>(hits.size()) > k) hits.resize(k);
  return hits;
}

std::string cardSnippet(const std::string &markdown, size_t limit) {
  const auto body = stripMarkdownLead(markdown);
  if (body.empty()) return {};
  size_t chars = 0;
  size_t byteLimit = body.size();
  for (size_t i = 0; i < body.size(); ++i) {
    if ((static_cast<unsigned char>(body[i]) & 0xC0) != 0x80) ++chars;
    if (chars > limit) {
      byteLimit = i;
      break;
    }
  }
  if (byteLimit == body.size()) return body;
  std::string cut = body.substr(0, byteLimit);
  const auto paragraph = cut.rfind("\n\n");
  if (paragraph != std::string::npos && paragraph >= limit / 2) cut = cut.substr(0, paragraph);
  else if (const auto space = cut.find_last_of(" \n"); space != std::string::npos) cut = cut.substr(0, space);
  return cut + "…";
}

} // namespace sessio::rag
