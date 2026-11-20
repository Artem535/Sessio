#pragma once

#include <string>
#include <vector>

namespace sessio::rag {

struct Chunk {
  std::string text;
  std::vector<std::string> path;  // document title followed by the heading trail
  std::vector<std::string> kinds; // block kinds in order; ":split" marks oversized pieces
};

struct DocumentInfo {
  std::string docId;
  std::string url;
  std::string title;
  std::string collection;
  std::string revision;
  std::string docType; // prd | kp | case | meeting | faq | process | other
  std::string primary; // entity the document is about (empty if none)
};

} // namespace sessio::rag
