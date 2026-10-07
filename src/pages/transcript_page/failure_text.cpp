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

} // namespace pcm::transcriptionui
