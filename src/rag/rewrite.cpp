#include "rewrite.h"

#include <array>
#include <cctype>

namespace sessio::rag {

namespace {

constexpr std::array<std::string_view, 11> kAnaphora = {"это", "эти",  "такой", "такая",
                                                       "такие", "такое", "его",   "её",
                                                       "их",  "там",  "вас"};

std::string_view trim(std::string_view s) {
  const auto space = [](char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
  };
  while (!s.empty() && space(s.front())) s.remove_prefix(1);
  while (!s.empty() && space(s.back())) s.remove_suffix(1);
  return s;
}

std::string lower(std::string_view s) {
  std::string out(s);
  for (size_t i = 0; i < out.size(); ++i) {
    const unsigned char c = out[i];
    if (c >= 'A' && c <= 'Z') out[i] = static_cast<char>(c + 32);
    else if (c == 0xD0 && i + 1 < out.size()) {
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

bool hasAnaphor(std::string_view lowered) {
  size_t i = 0;
  while (i < lowered.size()) {
    const unsigned char c = lowered[i];
    const bool wordByte = (c >= 'a' && c <= 'z') || c >= 0x80;
    if (!wordByte) {
      ++i;
      continue;
    }
    const size_t start = i;
    while (i < lowered.size()) {
      const unsigned char b = lowered[i];
      if (!((b >= 'a' && b <= 'z') || b >= 0x80)) break;
      ++i;
    }
    const std::string_view token(lowered.data() + start, i - start);
    for (const auto a : kAnaphora)
      if (token == a) return true;
  }
  return false;
}

} // namespace

std::string rewriteQuery(std::string_view utterance, std::string_view context) {
  auto text = trim(utterance);
  const auto lowered = lower(text);
  if (lowered.starts_with("а ") || lowered.starts_with("а,")) text = trim(text.substr(2));
  const auto topic = trim(context);
  if (!topic.empty() && hasAnaphor(lower(text))) return std::string(text) + " " + std::string(topic);
  return std::string(text);
}

std::string classifyCategory(std::string_view utterance) {
  const auto lowered = lower(utterance);
  const auto has = [&lowered](std::string_view needle) { return lowered.find(needle) != std::string::npos; };
  // Explicit "where is it stored" cues win over an incidental "проект":
  // "где лежат КП и оценки по проектам" is navigation, not a case question.
  // A bare "где делали проекты" (no storage noun) still falls through to cases.
  if (has("найти") || has("найди") || has("папк") || has("леж") || has("хран") ||
      has("договор") || has("файл")) return "navigation";
  if (has("кейс") || has("пример") || has("клиент") || has("внедрен") || has("проект") ||
      has("делали") || has("отрасл") || has("сфер") || has("направлен")) return "cases";
  if (has("статус") || has("этап") || has("когда") || has("срок")) return "status";
  if (has("сколько") || has("стоим") || has("цен") || has("бюджет")) return "pricing";
  if (has("интеграц") || has("подключ") || has("api")) return "integration";
  if (has("где") || has("лежит") || has("документ") || has("найди")) return "navigation";
  return "other";
}

} // namespace sessio::rag
