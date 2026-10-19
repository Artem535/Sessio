#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QString>
#include <functional>
#include <memory>

namespace pcm::prompter {

// A loaded local model. generate() runs on one worker thread at a time;
// cancel() may be called from any thread to abort the running generate(),
// which then throws.
class PrompterEngine {
public:
  virtual ~PrompterEngine() = default;
  virtual QJsonValue generate(const QString &system, const QByteArray &payload) = 0;
  virtual void cancel() = 0;
};

// Loads a model file. Runs on the worker thread. Returns null on failure.
using PrompterEngineFactory = std::function<std::unique_ptr<PrompterEngine>(const QString &modelPath)>;

struct GemmaOptions {
  // Process each fixed system prompt once and reuse its KV cache.
  bool reusePrefix = true;
};

// Gemma 4 E4B through LiteRT-LM on CPU with the settings validated in spike
// #146: 4 threads, 4096-token context, greedy decoding, speculative decoding,
// at most 384 output tokens. `cacheDir` keeps the compiled XNNPACK cache.
// Null when Sessio was built without LiteRT-LM or the model cannot be loaded.
std::unique_ptr<PrompterEngine> createGemmaEngine(const QString &modelPath, const QString &cacheDir,
                                                  const GemmaOptions &options = {});

// False when this build has no LiteRT-LM runtime.
bool gemmaRuntimeAvailable();

} // namespace pcm::prompter
