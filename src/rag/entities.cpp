#include "entities.h"

#include <algorithm>
#include <cctype>

namespace sessio::rag {

namespace {

std::string normalize(std::string_view text) {
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    const unsigned char c = text[i];
    if (c >= 'A' && c <= 'Z') out += static_cast<char>(c + 32);
    else if (c >= 'a' && c <= 'z') out += static_cast<char>(c);
    else if (c == 0xD0 && i + 1 < text.size()) {
      const unsigned char next = text[i + 1];
      if (next >= 0x90 && next <= 0x9F) out += static_cast<char>(0xD0), out += static_cast<char>(next + 0x20);
      else if (next >= 0xA0 && next <= 0xAF) out += static_cast<char>(0xD1), out += static_cast<char>(next - 0x20);
      else if (next == 0x81) out += static_cast<char>(0xD1), out += static_cast<char>(0xB5); // ё -> е
      else if (next >= 0xB0 && next <= 0xBF) out += static_cast<char>(0xD0), out += static_cast<char>(next);
      else out += ' ';
      ++i;
    } else if (c == 0xD1 && i + 1 < text.size()) {
      const unsigned char next = text[i + 1];
      if (next == 0x91) out += static_cast<char>(0xD0), out += static_cast<char>(0xB5);
      else if (next >= 0x80 && next <= 0x8F) out += static_cast<char>(0xD1), out += static_cast<char>(next);
      else out += ' ';
      ++i;
    } else if (c >= '0' && c <= '9') out += static_cast<char>(c);
    else out += ' ';
  }
  return out;
}

std::vector<std::string> tokens(const std::string &normalized) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < normalized.size()) {
    if (normalized[i] == ' ') {
      ++i;
      continue;
    }
    const size_t start = i;
    while (i < normalized.size() && normalized[i] != ' ') ++i;
    out.push_back(normalized.substr(start, i - start));
  }
  return out;
}

int editDistance(const std::u32string &a, const std::u32string &b, int cap) {
  if (std::abs(static_cast<int>(a.size()) - static_cast<int>(b.size())) > cap) return cap + 1;
  std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
  for (size_t i = 1; i <= a.size(); ++i) {
    cur[0] = static_cast<int>(i);
    int rowMin = cur[0];
    for (size_t j = 1; j <= b.size(); ++j) {
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
      rowMin = std::min(rowMin, cur[j]);
    }
    if (rowMin > cap) return cap + 1;
    std::swap(prev, cur);
  }
  return prev[b.size()];
}

std::u32string decode(std::string_view s) {
  std::u32string out;
  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = s[i];
    if (c < 0x80) out += static_cast<char32_t>(c);
    else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
      out += static_cast<char32_t>(((c & 0x1F) << 6) | (s[i + 1] & 0x3F));
      ++i;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
      out += static_cast<char32_t>(((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F));
      i += 2;
    } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
      out += static_cast<char32_t>(((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12)
                                   | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F));
      i += 3;
    }
  }
  return out;
}

bool tokenMatches(const std::string &spoken, const std::string &canonical) {
  if (spoken == canonical) return true;
  const auto a = decode(spoken), b = decode(canonical);
  if (b.size() >= 8) return editDistance(a, b, 2) <= 2;
  if (b.size() >= 4) return editDistance(a, b, 1) <= 1;
  return false;
}

size_t charCount(const std::string &s) {
  size_t n = 0;
  for (unsigned char c : s)
    if ((c & 0xC0) != 0x80) ++n;
  return n;
}

} // namespace

std::vector<EntityMatch> matchEntities(const std::string &utterance, const std::vector<Entity> &entities) {
  const auto spoken = tokens(normalize(utterance));
  std::vector<EntityMatch> matches;
  for (const auto &entity : entities) {
    const auto want = tokens(normalize(entity.name));
    if (want.empty() || want.size() > spoken.size()) continue;
    if (want.size() == 1 && charCount(want[0]) < 5) {
      if (std::find(spoken.begin(), spoken.end(), want[0]) != spoken.end())
        matches.push_back({entity.name, entity.kind, entity.docIds, entity.primaryDocs});
      continue;
    }
    bool found = false;
    for (size_t start = 0; start + want.size() <= spoken.size() && !found; ++start) {
      bool all = true;
      for (size_t k = 0; k < want.size(); ++k) {
        if (!tokenMatches(spoken[start + k], want[k])) {
          all = false;
          break;
        }
      }
      found = all;
    }
    if (found) matches.push_back({entity.name, entity.kind, entity.docIds, entity.primaryDocs});
    if (matches.size() >= 3) break;
  }
  return matches;
}

} // namespace sessio::rag
