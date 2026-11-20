#pragma once

#include "chunk.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace sessio::rag {

inline constexpr size_t kMaxChunkChars = 1800;

// Structure-first Markdown chunking, a faithful port of the 12.11 Outline
// spike: headings carry a title-rooted path, tables/lists/code stay whole
// when they fit, oversized blocks split with table header rows repeated.
// Lengths count Unicode code points, matching the archived corpus snapshot.
std::vector<Chunk> chunkMarkdown(std::string_view title, std::string_view text);

} // namespace sessio::rag
