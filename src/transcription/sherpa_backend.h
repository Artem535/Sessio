#pragma once

#include <memory>

#include "model_locator.h"
#include "speech_recognizer.h"
#include "transcription_engine.h"

namespace pcm::transcription {

std::shared_ptr<ISpeechRecognizer> makeSherpaRecognizer(const ModelPaths& paths, int num_threads = 4);

TranscriptionEngine::VadFactory makeSherpaVadFactory(const ModelPaths& paths);

}  // namespace pcm::transcription
