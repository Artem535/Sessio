#include "engine_factory.h"

#include <algorithm>
#include <exception>
#include <vector>
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

} // namespace

QString sanitizeLoadError(const char *what, const std::vector<std::filesystem::path> &roots) {
  QString text = QString::fromUtf8(what);
  // Cut known roots first (longest first) so a path containing spaces disappears whole.
  QStringList known;
  for (const auto &root : roots) {
    if (root.empty()) continue;
    known << QString::fromStdString(root.string()) << QString::fromStdString(root.generic_string());
  }
  known.removeDuplicates();
  std::sort(known.begin(), known.end(),
            [](const QString &a, const QString &b) { return a.size() > b.size(); });
  for (const QString &root : std::as_const(known)) {
    if (!root.isEmpty()) text.replace(root, QString());
  }
  // Whatever path is left (unknown roots): reduce each separator-containing token to its
  // base name.
  QStringList out;
  for (const QString &token : text.split(QLatin1Char(' '))) {
    const qsizetype cut =
        qMax(token.lastIndexOf(QLatin1Char('/')), token.lastIndexOf(QLatin1Char('\\')));
    out << (cut >= 0 ? token.mid(cut + 1) : token);
  }
  return out.join(QLatin1Char(' '));
}

EngineFactory makeProductionEngineFactory(std::filesystem::path appDir,
                                          std::string modelsEnvOverride) {
  return [appDir = std::move(appDir), env = std::move(modelsEnvOverride)](
             pcm::transcription::EngineCallbacks callbacks,
             QString *error) -> std::shared_ptr<pcm::transcription::TranscriptionEngine> {
    auto fail = [error](const QString &text) {
      if (error) *error = text;
      return std::shared_ptr<pcm::transcription::TranscriptionEngine>();
    };
    const std::vector<std::filesystem::path> roots = {
        appDir, appDir.parent_path(), std::filesystem::path(env),
        std::filesystem::path(env).parent_path()};
    try {
      const auto located = pcm::transcription::locateModels(
          appDir, pcm::transcription::currentPlatform(), env);
      if (!located.paths) return fail(sanitizedLocateError(located.error));
      auto recognizer = pcm::transcription::makeSherpaRecognizer(*located.paths);
      if (!recognizer) return fail(QStringLiteral("Speech recogniser could not be created"));
      auto vadFactory = pcm::transcription::makeSherpaVadFactory(*located.paths);
      // Probe once so a corrupt VAD model fails here with an error instead of the call
      // recording with no output when the first track creates its detector.
      if (!vadFactory || !vadFactory()) return fail(QStringLiteral("Voice activity model could not be created"));
      return std::make_shared<pcm::transcription::TranscriptionEngine>(
          std::move(vadFactory), std::move(recognizer), std::move(callbacks));
    } catch (const std::exception &e) {
      return fail(QStringLiteral("Speech model load failed: ") + sanitizeLoadError(e.what(), roots));
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

} // namespace pcm::calltranscription
