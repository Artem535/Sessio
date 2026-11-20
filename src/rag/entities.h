#pragma once

#include <string>
#include <vector>

namespace sessio::rag {

struct Entity {
  std::string name;
  std::string kind; // project | client | product
  std::vector<std::string> docIds;
  std::vector<std::string> primaryDocs; // docs ABOUT this entity (boost only these)
};

struct EntityMatch {
  std::string name; // canonical spelling from the wiki
  std::string kind;
  std::vector<std::string> docIds;
  std::vector<std::string> primaryDocs;
};

// Fuzzy entity recognition for STT-garbled utterances: normalized token
// windows matched against entity names with capped edit distance, so
// misheard project/client names still resolve to their canonical spelling.
std::vector<EntityMatch> matchEntities(const std::string &utterance, const std::vector<Entity> &entities);

} // namespace sessio::rag
