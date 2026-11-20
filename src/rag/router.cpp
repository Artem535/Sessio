#include "router.h"

#include <array>
#include <string>
#include <string_view>

namespace sessio::rag {
namespace {

constexpr std::array<std::string_view, 24> kQuestionWords = {
    "кто", "что", "где", "когда", "как", "какой", "какая", "какое", "какие",
    "каков", "какова", "каковы", "чей", "чья", "чьё", "чьи", "почему",
    "зачем", "сколько", "куда", "откуда", "ли", "разве", "неужели"};

constexpr std::array<std::string_view, 9> kRequestVerbs = {
    "расскажите", "покажите", "подскажите", "найдите", "отправьте",
    "поделитесь", "назовите", "перечислите", "объясните"};

bool isWordByte(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80;
}

// Lowercases ASCII and Cyrillic (А-Я, Ё) in place; other bytes pass through.
void lowerCyrillic(std::string &s) {
  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = s[i];
    if (c >= 'A' && c <= 'Z') {
      s[i] = static_cast<char>(c + ('a' - 'A'));
    } else if (c == 0xD0 && i + 1 < s.size()) {
      const unsigned char next = s[i + 1];
      if (next >= 0x90 && next <= 0x9F) s[i + 1] = static_cast<char>(next + 0x20);
      else if (next >= 0xA0 && next <= 0xAF) { s[i] = static_cast<char>(0xD1); s[i + 1] = static_cast<char>(next - 0x20); }
      else if (next == 0x81) { s[i] = static_cast<char>(0xD1); s[i + 1] = static_cast<char>(0x91); }
      ++i;
    }
  }
}

} // namespace

bool shouldSearch(std::string_view utterance) {
  if (utterance.find('?') != std::string_view::npos) return true;
  std::string lower(utterance);
  lowerCyrillic(lower);
  const std::string_view text = lower;
  size_t i = 0;
  while (i < text.size()) {
    if (!isWordByte(static_cast<unsigned char>(text[i]))) {
      ++i;
      continue;
    }
    const size_t start = i;
    while (i < text.size() && isWordByte(static_cast<unsigned char>(text[i]))) ++i;
    const std::string_view token(text.data() + start, i - start);
    for (const auto word : kQuestionWords)
      if (token == word) return true;
    for (const auto verb : kRequestVerbs)
      if (token == verb) return true;
  }
  return false;
}

} // namespace sessio::rag
