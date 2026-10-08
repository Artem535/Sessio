#pragma once

#include <QString>
#include <filesystem>
#include <string>
#include <vector>

#include "transcription_session.h"

namespace pcm::calltranscription {

// Production factory: locates the speech models, loads the recogniser and VAD
// and builds the engine. Never throws; returns null and sets *error on failure.
// Error texts are short diagnostics without directory paths.
EngineFactory makeProductionEngineFactory(std::filesystem::path appDir,
                                          std::string modelsEnvOverride = {});

// Removes the given directory roots (and, as a fallback, any other path prefix) from a
// load-error text so it never leaks a user name or folder. Handles roots with spaces.
QString sanitizeLoadError(const char *what, const std::vector<std::filesystem::path> &roots);

// True when all model files exist (does not load them). *reason receives a
// path-free diagnostic when false.
bool transcriptionModelsAvailable(const std::filesystem::path &appDir,
                                  const std::string &envOverride = {}, QString *reason = nullptr);

} // namespace pcm::calltranscription
