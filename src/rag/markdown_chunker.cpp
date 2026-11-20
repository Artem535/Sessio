#include "markdown_chunker.h"

#include <algorithm>
#include <utility>

namespace sessio::rag {
namespace {

constexpr std::string_view kSep = "\n\n";

enum class BlockKind { Heading, Code, Table, List, Quote, Para };

struct Block {
  BlockKind kind;
  std::string text;
  int tableId = -1;
};

const char *kindName(BlockKind kind) {
  switch (kind) {
  case BlockKind::Heading: return "heading";
  case BlockKind::Code: return "code";
  case BlockKind::Table: return "table";
  case BlockKind::List: return "list";
  case BlockKind::Quote: return "quote";
  case BlockKind::Para: return "para";
  }
  return "para";
}

bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

std::string_view lstrip(std::string_view s) {
  while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
  return s;
}

std::string_view strip(std::string_view s) {
  s = lstrip(s);
  while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
  return s;
}

bool isContinuationByte(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

size_t charCount(std::string_view s) {
  size_t n = 0;
  for (char c : s)
    if (!isContinuationByte(c)) ++n;
  return n;
}

// Byte offsets of every code point start, plus the end sentinel.
std::vector<size_t> codePointOffsets(std::string_view s) {
  std::vector<size_t> offsets;
  for (size_t i = 0; i < s.size(); ++i)
    if (!isContinuationByte(s[i])) offsets.push_back(i);
  offsets.push_back(s.size());
  return offsets;
}

std::vector<std::string_view> splitLines(std::string_view text) {
  std::vector<std::string_view> lines;
  size_t start = 0;
  while (true) {
    const size_t pos = text.find('\n', start);
    if (pos == std::string_view::npos) {
      lines.push_back(text.substr(start));
      break;
    }
    lines.push_back(text.substr(start, pos - start));
    start = pos + 1;
  }
  return lines;
}

std::string join(const std::vector<std::string> &parts, std::string_view sep) {
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

int headingLevel(std::string_view line) {
  size_t n = 0;
  while (n < line.size() && n < 6 && line[n] == '#') ++n;
  if (n == 0 || n >= line.size() || !isSpace(line[n])) return 0;
  return static_cast<int>(n);
}

std::string headingText(std::string_view line) {
  while (!line.empty() && line.front() == '#') line.remove_prefix(1);
  return std::string(strip(line));
}

bool isFence(std::string_view line) {
  line = lstrip(line);
  return line.starts_with("```") || line.starts_with("~~~");
}

bool isTableRow(std::string_view line) { return lstrip(line).starts_with('|'); }

bool isListItem(std::string_view line) {
  line = lstrip(line);
  if (line.empty()) return false;
  size_t i = 0;
  if (line[0] == '-' || line[0] == '*' || line[0] == '+') {
    i = 1;
  } else {
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') ++i;
    if (i == 0 || i >= line.size() || (line[i] != '.' && line[i] != ')')) return false;
    ++i;
  }
  return i < line.size() && isSpace(line[i]);
}

// Matches the spike's ^\s*\|[\s:-]+\| separator-row probe (prefix match).
bool isTableSeparator(std::string_view line) {
  line = lstrip(line);
  if (!line.starts_with('|')) return false;
  line.remove_prefix(1);
  size_t i = 0;
  while (i < line.size() && (isSpace(line[i]) || line[i] == ':' || line[i] == '-')) ++i;
  return i > 0 && i < line.size() && line[i] == '|';
}

std::vector<Block> parseBlocks(std::string_view text) {
  std::vector<Block> blocks;
  std::vector<std::string> buf;
  BlockKind kind = BlockKind::Para;
  bool inBlock = false;
  int tableSeq = 0;
  const auto flush = [&] {
    if (!buf.empty()) {
      Block block;
      block.kind = kind;
      block.text = std::string(strip(join(buf, "\n")));
      if (kind == BlockKind::Table) block.tableId = tableSeq++;
      blocks.push_back(std::move(block));
    }
    buf.clear();
    inBlock = false;
  };
  const auto start = [&](BlockKind next) {
    if (!inBlock || kind != next) {
      flush();
      kind = next;
      inBlock = true;
    }
  };
  for (const auto line : splitLines(text)) {
    if (headingLevel(line)) {
      flush();
      blocks.push_back({BlockKind::Heading, std::string(strip(line)), -1});
      continue;
    }
    if (isFence(line)) {
      if (inBlock && kind == BlockKind::Code) {
        buf.emplace_back(line);
        flush();
      } else {
        flush();
        kind = BlockKind::Code;
        inBlock = true;
        buf.emplace_back(line);
      }
      continue;
    }
    if (inBlock && kind == BlockKind::Code) {
      buf.emplace_back(line);
      continue;
    }
    if (isTableRow(line)) {
      start(BlockKind::Table);
      buf.emplace_back(line);
      continue;
    }
    if (isListItem(line) ||
        (inBlock && kind == BlockKind::List && line.starts_with("  ") && !strip(line).empty())) {
      start(BlockKind::List);
      buf.emplace_back(line);
      continue;
    }
    if (line.starts_with('>')) {
      start(BlockKind::Quote);
      buf.emplace_back(line);
      continue;
    }
    if (strip(line).empty()) {
      flush();
      continue;
    }
    start(BlockKind::Para);
    buf.emplace_back(line);
  }
  flush();
  return blocks;
}

std::vector<std::string> hardWindows(std::string_view text) {
  const auto offsets = codePointOffsets(text);
  const size_t chars = offsets.size() - 1;
  std::vector<std::string> windows;
  for (size_t i = 0; i < chars; i += kMaxChunkChars) {
    const size_t end = std::min(i + kMaxChunkChars, chars);
    windows.emplace_back(text.substr(offsets[i], offsets[end] - offsets[i]));
  }
  return windows;
}

std::vector<std::string> splitOversized(const std::string &blockText, BlockKind kind) {
  const auto lines = splitLines(blockText);
  std::vector<std::string> header;
  if (kind == BlockKind::Table && lines.size() > 1 && isTableSeparator(lines[1]))
    header = {std::string(lines[0]), std::string(lines[1])};
  std::vector<std::string> pieces;
  std::vector<std::string> cur = header;
  for (size_t i = header.empty() ? 0 : 2; i < lines.size(); ++i) {
    auto candidate = cur;
    candidate.emplace_back(lines[i]);
    if (charCount(join(candidate, "\n")) > kMaxChunkChars && cur != header) {
      pieces.push_back(join(cur, "\n"));
      cur = header;
    }
    cur.emplace_back(lines[i]);
  }
  if (!cur.empty() && cur != header) pieces.push_back(join(cur, "\n"));
  if (pieces.empty()) pieces.push_back(blockText);
  std::vector<std::string> final;
  for (const auto &piece : pieces) {
    auto windows = hardWindows(piece);
    final.insert(final.end(), std::make_move_iterator(windows.begin()), std::make_move_iterator(windows.end()));
  }
  if (final.empty()) final.push_back(blockText);
  return final;
}

} // namespace

std::vector<Chunk> chunkMarkdown(std::string_view title, std::string_view text) {
  std::vector<Chunk> chunks;
  std::vector<std::string> cur;
  std::vector<std::string> curKinds;
  std::vector<std::string> path{std::string(title)};
  const auto emit = [&] {
    if (!cur.empty()) chunks.push_back(Chunk{join(cur, kSep), path, curKinds});
    cur.clear();
    curKinds.clear();
  };
  for (const auto &block : parseBlocks(text)) {
    if (block.kind == BlockKind::Heading) {
      emit();
      const size_t level = static_cast<size_t>(headingLevel(block.text));
      std::vector<std::string> next(path.begin(), path.begin() + std::min(level, path.size()));
      next.push_back(headingText(block.text));
      path = std::move(next);
      continue;
    }
    if (charCount(block.text) > kMaxChunkChars) {
      emit();
      for (auto &piece : splitOversized(block.text, block.kind))
        chunks.push_back(Chunk{std::move(piece), path, {std::string(kindName(block.kind)) + ":split"}});
      continue;
    }
    size_t joined = charCount(block.text) + kSep.size() * cur.size();
    for (const auto &c : cur) joined += charCount(c);
    if (!cur.empty() && joined > kMaxChunkChars) emit();
    cur.push_back(block.text);
    curKinds.emplace_back(kindName(block.kind));
  }
  emit();
  return chunks;
}

} // namespace sessio::rag
