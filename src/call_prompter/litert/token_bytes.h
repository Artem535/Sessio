#pragma once

#include <string>

namespace sessio::litert::detail {
inline std::string sentencePieceTokenBytes(std::string piece) {
  // GetTokens() returns vocabulary pieces; llguidance expects decoded bytes.
  const auto hex = [](char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  if (piece.size() == 6 && piece.starts_with("<0x") && piece.back() == '>') {
    const int high = hex(piece[3]), low = hex(piece[4]);
    if (high >= 0 && low >= 0) return std::string(1, static_cast<char>((high << 4) | low));
  }
  for (size_t pos = 0; (pos = piece.find("▁", pos)) != std::string::npos; ++pos)
    piece.replace(pos, 3, " ");
  return piece;
}
} // namespace sessio::litert::detail
