// Sanity harness for the rag-core embedding engine: reads length-prefixed
// texts, embeds them through libsessio_litert, writes a raw float32 matrix.
// Build: ./build.sh   Run: see sanity.py

#include "sessio_litert.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::vector<std::string> readTexts(const char *path) {
  std::ifstream in(path, std::ios::binary);
  std::vector<std::string> texts;
  uint32_t len = 0;
  while (in.read(reinterpret_cast<char *>(&len), sizeof(len))) {
    std::string text(len, '\0');
    in.read(text.data(), len);
    texts.push_back(std::move(text));
  }
  return texts;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 5) {
    std::fprintf(stderr, "usage: embed_sanity model texts.bin out.bin batch [threads] [cache_dir]\n");
    return 2;
  }
  const int batch = std::atoi(argv[4]);
  sessio::litert::EmbedderOptions options;
  options.threads = argc > 5 ? std::atoi(argv[5]) : 4;
  if (argc > 6) options.cacheDir = argv[6];

  std::string error;
  const auto loadStart = Clock::now();
  auto embedder = sessio::litert::loadEmbedder(argv[1], options, &error);
  if (!embedder) {
    std::fprintf(stderr, "load failed: %s\n", error.c_str());
    return 1;
  }
  std::fprintf(stderr, "load_ms=%.0f threads=%d batch=%d\n", msSince(loadStart), options.threads, batch);

  const auto texts = readTexts(argv[2]);
  if (texts.empty()) {
    std::fprintf(stderr, "no texts\n");
    return 1;
  }
  std::vector<std::vector<float>> vectors;
  {
    std::vector<std::vector<float>> warmup;
    if (embedder->embed({"warmup"}, &warmup) != sessio::litert::Status::Ok) {
      std::fprintf(stderr, "warmup failed\n");
      return 1;
    }
  }
  const auto totalStart = Clock::now();
  for (size_t start = 0; start < texts.size(); start += static_cast<size_t>(batch)) {
    const size_t end = std::min(start + static_cast<size_t>(batch), texts.size());
    const std::vector<std::string> slice(texts.begin() + start, texts.begin() + end);
    std::vector<std::vector<float>> part;
    const auto batchStart = Clock::now();
    if (embedder->embed(slice, &part) != sessio::litert::Status::Ok || part.size() != slice.size()) {
      std::fprintf(stderr, "embed failed at %zu\n", start);
      return 1;
    }
    std::fprintf(stderr, "batch %zu n=%zu ms=%.0f\n", start, slice.size(), msSince(batchStart));
    vectors.insert(vectors.end(), part.begin(), part.end());
  }
  const double totalMs = msSince(totalStart);
  const uint32_t n = static_cast<uint32_t>(vectors.size());
  const uint32_t dim = static_cast<uint32_t>(vectors.front().size());
  std::ofstream out(argv[3], std::ios::binary);
  out.write(reinterpret_cast<const char *>(&n), sizeof(n));
  out.write(reinterpret_cast<const char *>(&dim), sizeof(dim));
  for (const auto &vector : vectors) {
    if (vector.size() != dim) {
      std::fprintf(stderr, "inconsistent dimensions\n");
      return 1;
    }
    out.write(reinterpret_cast<const char *>(vector.data()), static_cast<std::streamsize>(dim * sizeof(float)));
  }
  std::fprintf(stderr, "done n=%u dim=%u total_ms=%.0f ms_per_text=%.1f\n", n, dim, totalMs, totalMs / n);
  return 0;
}
