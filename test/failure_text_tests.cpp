#include "failure_text.h"
#include <QCoreApplication>
#include <gtest/gtest.h>

using pcm::transcriptionui::userFacingFailure;

TEST(TranscriptionFailureTextTest, KnownFailuresHaveDistinctSafeMessages) {
  const QString save = QStringLiteral("Could not save the transcript");
  const QString missing = QStringLiteral("Model file not found: /home/private/model.onnx");
  const QString vad = QStringLiteral("Voice activity model could not be created");
  const QString unknown = QStringLiteral("private track identifier and diagnostic");
  const QStringList inputs{save, missing, vad, unknown};
  QStringList outputs;
  for (const auto &input : inputs) {
    const auto text = userFacingFailure(input);
    EXPECT_FALSE(text.isEmpty());
    EXPECT_FALSE(text.contains(input));
    EXPECT_FALSE(outputs.contains(text));
    outputs.append(text);
  }
}

TEST(TranscriptionFailureTextTest, UnknownDiagnosticsNeverLeakAndUseGenericFallback) {
  const auto generic = userFacingFailure({});
  for (const auto &raw : {QStringLiteral("secret-client-id"), QStringLiteral("/home/private/audio.wav"),
                          QStringLiteral("Could not save the transcript: private details")}) {
    EXPECT_EQ(userFacingFailure(raw), generic);
    EXPECT_FALSE(userFacingFailure(raw).contains(raw));
  }
}

TEST(TranscriptionFailureTextTest, ActualEngineFactoryReasonsAreMapped) {
  const auto generic = userFacingFailure({});
  for (const auto &raw : {QStringLiteral("Speech recogniser could not be created"),
                          QStringLiteral("Speech model load failed: private detail"),
                          QStringLiteral("Speech model load failed"),
                          QStringLiteral("Could not load the transcription model"),
                          QStringLiteral("Transcription models are not installed"),
                          QStringLiteral("Model lookup failed")}) {
    EXPECT_NE(userFacingFailure(raw), generic);
    EXPECT_FALSE(userFacingFailure(raw).contains(raw));
  }
}
