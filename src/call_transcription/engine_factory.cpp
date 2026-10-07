#include "engine_factory.h"

#include <exception>
#include <QStringList>
#include <memory>

#include "model_locator.h"
#include "sherpa_backend.h"

namespace pcm::calltranscription {

namespace {

// locateModels() appends the searched directory ("looked in <root>"), which may
// contain a user name; keep only the part before it.
QString sanitizedLocateError(const std::string &error) {
  QString text = QString::fromStdString(error);
  const qsizetype cut = text.indexOf(QStringLiteral(" (looked in"));
  if (cut >= 0) text.truncate(cut);
  if (text.isEmpty()) text = QStringLiteral("Transcription models are not installed");
  return text;
}

// Exception texts from the sherpa load may embed full model paths; reduce every
// whitespace-delimited token that contains a path separator to its base name.
QString stripPaths(const char *what) {
  const QStringList tokens = QString::fromUtf8(what).split(QLatin1Char(' '));
  QStringList out;
  for (const QString &token : tokens) {
    const qsizetype cut = qMax(token.lastIndexOf(QLatin1Char('/')), token.lastIndexOf(QLatin1Char('\\')));
    out << (cut >= 0 ? token.mid(cut + 1) : token);
  }
  return out.join(QLatin1Char(' '));
}

}  // namespace

EngineFactory makeProductionEngineFactory(std::filesystem::path appDir,
                                          std::string modelsEnvOverride) {
  return [appDir = std::move(appDir), env = std::move(modelsEnvOverride)](
             pcm::transcription::EngineCallbacks callbacks,
             QString *error) -> std::shared_ptr<pcm::transcription::TranscriptionEngine> {
    auto fail = [error](const QString &text) {
      if (error) *error = text;
      return std::shared_ptr<pcm::transcription::TranscriptionEngine>();
    };
    try {
      const auto located = pcm::transcription::locateModels(
          appDir, pcm::transcription::currentPlatform(), env);
      if (!located.paths) return fail(sanitizedLocateError(located.error));
      auto recognizer = pcm::transcription::makeSherpaRecognizer(*located.paths);
      if (!recognizer) return fail(QStringLiteral("Speech recogniser could not be created"));
      auto vadFactory = pcm::transcription::makeSherpaVadFactory(*located.paths);
      return std::make_shared<pcm::transcription::TranscriptionEngine>(
          std::move(vadFactory), std::move(recognizer), std::move(callbacks));
    } catch (const std::exception &e) {
      return fail(QStringLiteral("Speech model load failed: ") + stripPaths(e.what()));
    } catch (...) {
      return fail(QStringLiteral("Speech model load failed"));
    }
  };
}

bool transcriptionModelsAvailable(const std::filesystem::path &appDir, const std::string &envOverride,
                                  QString *reason) {
  try {
    const auto located = pcm::transcription::locateModels(
        appDir, pcm::transcription::currentPlatform(), envOverride);
    if (located.paths) return true;
    if (reason) *reason = sanitizedLocateError(located.error);
  } catch (...) {
    if (reason) *reason = QStringLiteral("Model lookup failed");
  }
  return false;
}

}  // namespace pcm::calltranscription
