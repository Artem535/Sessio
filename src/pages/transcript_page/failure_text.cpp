#include "failure_text.h"

#include <QCoreApplication>

namespace pcm::transcriptionui {

QString userFacingFailure(const QString &raw) {
  if (raw == QStringLiteral("Could not save the transcript")) {
    return QCoreApplication::translate("TranscriptionFailure", "The transcript could not be saved. Please try again.");
  }
  if (raw.startsWith(QStringLiteral("Model file not found")) ||
      raw == QStringLiteral("Transcription models are not installed") ||
      raw == QStringLiteral("Model lookup failed")) {
    return QCoreApplication::translate("TranscriptionFailure", "The transcription models are unavailable. Please check the model installation.");
  }
  if (raw == QStringLiteral("Voice activity model could not be created")) {
    return QCoreApplication::translate("TranscriptionFailure", "The voice activity detector could not be started. Please check the model installation.");
  }
  if (raw == QStringLiteral("Speech recogniser could not be created") ||
      raw == QStringLiteral("Speech model load failed") ||
      raw.startsWith(QStringLiteral("Speech model load failed: ")) ||
      raw == QStringLiteral("Could not load the transcription model")) {
    return QCoreApplication::translate("TranscriptionFailure", "The speech recognition model could not be loaded. Please check the model installation.");
  }
  return QCoreApplication::translate("TranscriptionFailure", "Transcription could not be completed. Please try again.");
}

QString cloudFailureText(const QString &detail) {
  const auto prefix = QCoreApplication::translate("CloudFailure", "Cloud unavailable");
  QString reason;
  if (detail == QLatin1String("auth")) reason = QCoreApplication::translate("CloudFailure", "the API key was rejected");
  else if (detail == QLatin1String("model")) reason = QCoreApplication::translate("CloudFailure", "the model was not found");
  else if (detail == QLatin1String("no_key") || detail == QLatin1String("no_engine")) reason = QCoreApplication::translate("CloudFailure", "no API key is set");
  else if (detail == QLatin1String("unsupported")) reason = QCoreApplication::translate("CloudFailure", "the provider does not support structured output");
  else if (detail == QLatin1String("rate_limit")) reason = QCoreApplication::translate("CloudFailure", "the provider is limiting requests, try again later");
  else if (detail == QLatin1String("server")) reason = QCoreApplication::translate("CloudFailure", "the provider reported an error");
  else if (detail == QLatin1String("network")) reason = QCoreApplication::translate("CloudFailure", "no connection to the provider");
  else if (detail == QLatin1String("timeout")) reason = QCoreApplication::translate("CloudFailure", "the provider took too long to answer");
  else reason = QCoreApplication::translate("CloudFailure", "the provider rejected the request");
  return prefix + QStringLiteral(": ") + reason + QLatin1Char('.');
}

QString engineLabel(const QString &raw) {
  if (raw == QLatin1String("local")) return QCoreApplication::translate("Engine", "Local model");
  if (raw.startsWith(QLatin1String("cloud"))) {
    const auto model = raw.mid(6);
    return model.isEmpty() ? QCoreApplication::translate("Engine", "Cloud")
                           : QCoreApplication::translate("Engine", "Cloud · %1").arg(model);
  }
  return {};
}

bool cloudFailureIsTransient(const QString &detail) {
  return detail == QLatin1String("network") || detail == QLatin1String("server") ||
         detail == QLatin1String("rate_limit") || detail == QLatin1String("timeout");
}

} // namespace pcm::transcriptionui
