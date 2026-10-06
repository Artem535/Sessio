// Throwaway spike code (issue #116) — not part of the Sessio build.
// GigaAM v3 (offline transducer) behind Silero VAD through sherpa-onnx's C++ API.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "sherpa-onnx/c-api/cxx-api.h"

using Clock = std::chrono::steady_clock;

static double Since(Clock::time_point t0) {
  return std::chrono::duration<double>(Clock::now() - t0).count();
}

int main(int argc, char** argv) {
  using namespace sherpa_onnx::cxx;  // NOLINT
  if (argc < 4) {
    std::cerr << "usage: asr_smoke_cxx <gigaam_dir> <silero_vad.onnx> <wav>\n";
    return 2;
  }
  const std::string dir = argv[1];

  OfflineRecognizerConfig config;
  config.feat_config.sample_rate = 16000;
  config.feat_config.feature_dim = 80;
  config.model_config.transducer.encoder = dir + "/encoder.int8.onnx";
  config.model_config.transducer.decoder = dir + "/decoder.onnx";
  config.model_config.transducer.joiner = dir + "/joiner.onnx";
  config.model_config.tokens = dir + "/tokens.txt";
  config.model_config.model_type = "nemo_transducer";
  config.model_config.num_threads = 4;
  OfflineRecognizer recognizer = OfflineRecognizer::Create(config);
  if (!recognizer.Get()) {
    std::cerr << "failed to create recognizer\n";
    return 1;
  }

  VadModelConfig vad_config;
  vad_config.silero_vad.model = argv[2];
  vad_config.silero_vad.min_silence_duration = 0.4F;
  vad_config.silero_vad.min_speech_duration = 0.25F;
  vad_config.silero_vad.max_speech_duration = 20.0F;
  vad_config.sample_rate = 16000;
  VoiceActivityDetector vad = VoiceActivityDetector::Create(vad_config, 120.0F);
  const int32_t window = vad_config.silero_vad.window_size;

  Wave wave = ReadWave(argv[3]);
  if (wave.samples.empty() || wave.sample_rate != 16000) {
    std::cerr << "need a non-empty 16 kHz wav\n";
    return 1;
  }

  double compute = 0.0;
  int phrases = 0;
  std::string text;
  auto drain = [&]() {
    while (!vad.IsEmpty()) {
      SpeechSegment seg = vad.Front();
      vad.Pop();
      const auto t0 = Clock::now();
      OfflineStream stream = recognizer.CreateStream();
      stream.AcceptWaveform(wave.sample_rate, seg.samples.data(), seg.samples.size());
      recognizer.Decode(&stream);
      std::string t = recognizer.GetResult(&stream).text;
      compute += Since(t0);
      if (!t.empty()) {
        ++phrases;
        text += (text.empty() ? "" : " ") + t;
      }
    }
  };

  const size_t step = static_cast<size_t>(wave.sample_rate / 10);  // 100 ms
  std::vector<float> pending;
  for (size_t i = 0; i < wave.samples.size(); i += step) {
    const size_t n = std::min(step, wave.samples.size() - i);
    const auto t0 = Clock::now();
    pending.insert(pending.end(), wave.samples.begin() + i, wave.samples.begin() + i + n);
    while (pending.size() >= static_cast<size_t>(window)) {
      vad.AcceptWaveform(pending.data(), window);
      pending.erase(pending.begin(), pending.begin() + window);
    }
    compute += Since(t0);
    drain();
  }
  vad.Flush();
  drain();

  const double duration = static_cast<double>(wave.samples.size()) / wave.sample_rate;
  std::cout << "text: " << text << "\n";
  std::printf("phrases %d  duration %.1fs  compute %.2fs  RTF %.3f\n", phrases, duration, compute,
              compute / duration);
  return 0;
}
