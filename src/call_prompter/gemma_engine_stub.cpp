#include "prompter_engine.h"

// Built when LiteRT-LM is not available for this platform: the prompter page
// stays visible but reports that the local runtime is missing.
namespace pcm::prompter {

bool gemmaRuntimeAvailable() { return false; }

std::unique_ptr<PrompterEngine> createGemmaEngine(const QString &, const QString &) { return nullptr; }

} // namespace pcm::prompter
