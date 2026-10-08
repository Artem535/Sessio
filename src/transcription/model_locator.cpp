#include "model_locator.h"

#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace pcm::transcription {

Platform currentPlatform() {
#if defined(_WIN32)
  return Platform::Windows;
#elif defined(__APPLE__)
  return Platform::MacOS;
#else
  return Platform::Linux;
#endif
}

std::vector<fs::path> modelRootCandidates(const fs::path& app_dir, Platform platform,
                                          std::string_view env_override) {
  std::vector<fs::path> out;
  if (!env_override.empty()) out.emplace_back(std::string(env_override));
  switch (platform) {
    case Platform::Linux: out.push_back(app_dir / ".." / "share" / "sessio" / "models"); break;
    case Platform::MacOS: out.push_back(app_dir / ".." / "Resources" / "models"); break;
    case Platform::Windows: out.push_back(app_dir / "models"); break;
  }
  return out;
}

LocateResult locateModels(const fs::path& app_dir, Platform platform, std::string_view env_override) {
  std::string error;
  for (const fs::path& root : modelRootCandidates(app_dir, platform, env_override)) {
    const fs::path dir = root / "gigaam-v3-rnnt";
    ModelPaths paths{dir / "encoder.int8.onnx", dir / "decoder.onnx", dir / "joiner.onnx",
                     dir / "tokens.txt", root / "silero_vad.onnx"};
    const fs::path* missing = nullptr;
    for (const fs::path* p : {&paths.encoder, &paths.decoder, &paths.joiner, &paths.tokens, &paths.vad}) {
      std::error_code ec;
      if (!fs::is_regular_file(*p, ec)) { missing = p; break; }
    }
    if (!missing) return {std::move(paths), {}};
    error = "Model file not found: " + missing->filename().string() + " (looked in " + root.string() + ")";
  }
  if (error.empty()) error = "Transcription models are not installed";
  return {std::nullopt, error};
}

}  // namespace pcm::transcription
