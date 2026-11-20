#include "display_name.h"
#include <vector>
namespace pcm::tokenbackend {
namespace {
bool whitespace(unsigned c) {
  return (c >= 9 && c <= 13) || c == 32 || c == 0xA0 || c == 0x1680 ||
      (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 ||
      c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF;
}
}
std::optional<std::string> normaliseDisplayName(std::string_view value, bool required) {
  struct Scalar { unsigned code; size_t begin, end; };
  std::vector<Scalar> scalars;
  for (size_t i = 0; i < value.size();) {
    const size_t begin = i;
    unsigned c = static_cast<unsigned char>(value[i++]);
    unsigned length = 0, minimum = 0;
    if (c >= 0xC2 && c <= 0xDF) { c &= 31; length = 1; minimum = 0x80; }
    else if (c >= 0xE0 && c <= 0xEF) { c &= 15; length = 2; minimum = 0x800; }
    else if (c >= 0xF0 && c <= 0xF4) { c &= 7; length = 3; minimum = 0x10000; }
    else if (c >= 0x80) return {};
    for (unsigned j = 0; j < length; ++j) {
      if (i == value.size()) return {};
      const unsigned next = static_cast<unsigned char>(value[i++]);
      if ((next & 0xC0) != 0x80) return {};
      c = (c << 6) | (next & 63);
    }
    if (c < minimum || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return {};
    scalars.push_back({c, begin, i});
  }
  size_t first = 0, last = scalars.size();
  while (first < last && whitespace(scalars[first].code)) ++first;
  while (last > first && whitespace(scalars[last - 1].code)) --last;
  if (last - first > 80 || (required && last == first)) return {};
  for (size_t i = first; i < last; ++i)
    if (scalars[i].code < 32 || (scalars[i].code >= 127 && scalars[i].code <= 159)) return {};
  if (first == last) return std::string{};
  return std::string(value.substr(scalars[first].begin, scalars[last - 1].end - scalars[first].begin));
}
}
