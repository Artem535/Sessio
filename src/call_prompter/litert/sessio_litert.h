#pragma once

// Boundary of libsessio_litert, the shared library that links the LiteRT-LM
// C++ runtime. Only this interface is exported; LiteRT-LM, Abseil, Protobuf
// and their bundled third-party code stay hidden inside the library so they
// cannot clash with Sessio's own copies. No Qt and no Abseil types here.

#include <memory>
#include <string>

#if defined(_WIN32)
#  if defined(SESSIO_LITERT_BUILD)
#    define SESSIO_LITERT_API __declspec(dllexport)
#  else
#    define SESSIO_LITERT_API __declspec(dllimport)
#  endif
#else
#  define SESSIO_LITERT_API __attribute__((visibility("default")))
#endif

namespace sessio::litert {

struct ModelOptions {
  int threads = 4;
  int contextTokens = 4096;
  std::string cacheDir; // compiled XNNPACK cache; empty disables it
  // Load the MTP drafter. Only used by generate() calls without reusePrefix.
  bool speculativeDecoding = false;
};

struct GenerateOptions {
  int maxOutputTokens = 384;
  int seed = 146;
  // Keep the processed prefix (its KV cache) and reuse it for later calls with
  // the same prefix instead of processing it again.
  bool reusePrefix = true;
};

// Work done by one generate() call, for timing diagnostics.
struct GenerateStats {
  int prefillTokens = 0;
  double prefillSeconds = 0;
  int decodeTokens = 0;
  double decodeSeconds = 0;
  bool prefixReused = false;
};

enum class Status { Ok, Cancelled, ContextExceeded, Truncated, Failed };

// One loaded text model. generate() is called from one thread at a time;
// cancel() may be called from any thread and aborts the running generate().
class SESSIO_LITERT_API Model {
public:
  virtual ~Model();
  // Runs one already formatted prompt, prefix + suffix (without <bos>), with
  // greedy decoding. The prefix should end at a special-token boundary, e.g.
  // right before the user turn, so splitting does not change tokenisation.
  virtual Status generate(const std::string &prefix, const std::string &suffix, const GenerateOptions &options,
                          std::string *reply, GenerateStats *stats = nullptr) = 0;
  virtual void cancel() = 0;
};

// Loads a .litertlm model on the CPU backend. Null on failure; the reason is
// a short diagnostic without paths or prompt text.
SESSIO_LITERT_API std::unique_ptr<Model> loadModel(const std::string &path, const ModelOptions &options,
                                                   std::string *error);

} // namespace sessio::litert
