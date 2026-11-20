// One-off fidelity check: reads (title, text) records, writes chunk records.
#include "markdown_chunker.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static std::string readStr(std::ifstream &in) {
  uint32_t len = 0;
  if (!in.read(reinterpret_cast<char *>(&len), sizeof(len))) return {};
  std::string s(len, '\0');
  in.read(s.data(), len);
  return s;
}

static void writeStr(std::ofstream &out, const std::string &s) {
  const uint32_t len = static_cast<uint32_t>(s.size());
  out.write(reinterpret_cast<const char *>(&len), sizeof(len));
  out.write(s.data(), len);
}

int main(int argc, char **argv) {
  if (argc < 3) return 2;
  std::ifstream in(argv[1], std::ios::binary);
  std::ofstream out(argv[2], std::ios::binary);
  while (in.peek() != EOF) {
    const auto title = readStr(in);
    const auto text = readStr(in);
    const auto chunks = sessio::rag::chunkMarkdown(title, text);
    const uint32_t n = static_cast<uint32_t>(chunks.size());
    out.write(reinterpret_cast<const char *>(&n), sizeof(n));
    for (const auto &chunk : chunks) {
      writeStr(out, chunk.text);
      std::string path, kinds;
      for (size_t i = 0; i < chunk.path.size(); ++i) { if (i) path += '\n'; path += chunk.path[i]; }
      for (size_t i = 0; i < chunk.kinds.size(); ++i) { if (i) kinds += ','; kinds += chunk.kinds[i]; }
      writeStr(out, path);
      writeStr(out, kinds);
    }
  }
  return 0;
}
