#include "sherpa_backend.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

#include "sherpa-onnx/c-api/cxx-api.h"

namespace pcm::transcription {

namespace {

namespace sc = sherpa_onnx::cxx;

class SherpaRecognizer : public ISpeechRecognizer {
 public:
  SherpaRecognizer(const ModelPaths& paths, int num_threads)
      : recognizer_(create(paths, num_threads)) {
    if (!recognizer_.Get()) throw std::runtime_error("Failed to load the recognition model");
  }

  std::string transcribe(std::span<const float> samples) override {
    sc::OfflineStream stream = recognizer_.CreateStream();
    stream.AcceptWaveform(16000, samples.data(), static_cast<int32_t>(samples.size()));
    recognizer_.Decode(&stream);
    return recognizer_.GetResult(&stream).text;
  }

 private:
  static sc::OfflineRecognizer create(const ModelPaths& paths, int num_threads) {
    sc::OfflineRecognizerConfig config;
    config.feat_config.sample_rate = 16000;
    config.feat_config.feature_dim = 80;
    config.model_config.transducer.encoder = paths.encoder.string();
    config.model_config.transducer.decoder = paths.decoder.string();
    config.model_config.transducer.joiner = paths.joiner.string();
    config.model_config.tokens = paths.tokens.string();
    config.model_config.model_type = "nemo_transducer";
    config.model_config.num_threads = num_threads;
    return sc::OfflineRecognizer::Create(config);
  }

  sc::OfflineRecognizer recognizer_;
};

class SherpaVad : public IVoiceActivityDetector {
 public:
  explicit SherpaVad(const ModelPaths& paths) : vad_(create(paths)) {
    if (!vad_.Get()) throw std::runtime_error("Failed to load the voice detector");
  }

  size_t windowSize() const override { return kWindow; }

  void accept(const float* window, size_t n) override {
    vad_.AcceptWaveform(window, static_cast<int32_t>(n));
  }

  bool hasSegment() const override { return !vad_.IsEmpty(); }

  SpeechSegment popSegment() override {
    sc::SpeechSegment seg = vad_.Front();
    vad_.Pop();
    return {static_cast<int64_t>(seg.start), std::move(seg.samples)};
  }

  void flush() override { vad_.Flush(); }

 private:
  static constexpr size_t kWindow = 512;

  static sc::VoiceActivityDetector create(const ModelPaths& paths) {
    sc::VadModelConfig config;
    config.silero_vad.model = paths.vad.string();
    config.silero_vad.threshold = 0.5F;
    config.silero_vad.min_silence_duration = 0.4F;
    config.silero_vad.min_speech_duration = 0.25F;
    config.silero_vad.max_speech_duration = 20.0F;
    config.sample_rate = 16000;
    config.silero_vad.window_size = static_cast<int32_t>(kWindow);
    return sc::VoiceActivityDetector::Create(config, 120.0F);
  }

  sc::VoiceActivityDetector vad_;
};

}  // namespace

std::shared_ptr<ISpeechRecognizer> makeSherpaRecognizer(const ModelPaths& paths, int num_threads) {
  return std::make_shared<SherpaRecognizer>(paths, num_threads);
}

TranscriptionEngine::VadFactory makeSherpaVadFactory(const ModelPaths& paths) {
  return [paths] { return std::make_unique<SherpaVad>(paths); };
}

}  // namespace pcm::transcription
