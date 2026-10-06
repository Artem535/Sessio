// Throwaway spike code (issue #116) — not part of the Sessio build.
// GigaAM v3 (offline transducer) behind Silero VAD through sherpa-onnx's *C* API: feeds a wav in
// 100 ms chunks like a live track, decodes every phrase the VAD closes, prints text and RTF.
//
// The C API is used on purpose: the prebuilt Linux shared libs are built with the old
// (pre-C++11) std::string ABI, so the C++ wrapper (cxx-api.h) does not link or run correctly
// against a modern toolchain.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sherpa-onnx/c-api/c-api.h"

using Clock = std::chrono::steady_clock;

static double Since(Clock::time_point t0) {
  return std::chrono::duration<double>(Clock::now() - t0).count();
}

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "usage: asr_smoke <gigaam_dir> <silero_vad.onnx> <wav>\n";
    return 2;
  }
  const std::string dir = argv[1];
  const std::string encoder = dir + "/encoder.int8.onnx";
  const std::string decoder = dir + "/decoder.onnx";
  const std::string joiner = dir + "/joiner.onnx";
  const std::string tokens = dir + "/tokens.txt";

  SherpaOnnxOfflineRecognizerConfig config;
  std::memset(&config, 0, sizeof(config));
  config.feat_config.sample_rate = 16000;
  config.feat_config.feature_dim = 80;
  config.model_config.transducer.encoder = encoder.c_str();
  config.model_config.transducer.decoder = decoder.c_str();
  config.model_config.transducer.joiner = joiner.c_str();
  config.model_config.tokens = tokens.c_str();
  config.model_config.model_type = "nemo_transducer";
  config.model_config.num_threads = 4;
  config.model_config.provider = "cpu";
  config.decoding_method = "greedy_search";
  const SherpaOnnxOfflineRecognizer* recognizer = SherpaOnnxCreateOfflineRecognizer(&config);
  if (!recognizer) {
    std::cerr << "failed to create recognizer; check gigaam_dir\n";
    return 1;
  }

  SherpaOnnxVadModelConfig vad_config;
  std::memset(&vad_config, 0, sizeof(vad_config));
  vad_config.silero_vad.model = argv[2];
  vad_config.silero_vad.threshold = 0.5F;
  vad_config.silero_vad.min_silence_duration = 0.4F;
  vad_config.silero_vad.min_speech_duration = 0.25F;
  vad_config.silero_vad.window_size = 512;
  vad_config.silero_vad.max_speech_duration = 20.0F;
  vad_config.sample_rate = 16000;
  vad_config.num_threads = 1;
  vad_config.provider = "cpu";
  const SherpaOnnxVoiceActivityDetector* vad = SherpaOnnxCreateVoiceActivityDetector(&vad_config, 120.0F);
  if (!vad) {
    std::cerr << "failed to create VAD; check silero_vad.onnx path\n";
    return 1;
  }
  const int32_t window = vad_config.silero_vad.window_size;

  const SherpaOnnxWave* wave = SherpaOnnxReadWave(argv[3]);
  if (!wave || wave->num_samples == 0 || wave->sample_rate != 16000) {
    std::cerr << "need a non-empty 16 kHz wav\n";
    return 1;
  }

  double compute = 0.0;
  int phrases = 0;
  std::string text;
  auto drain = [&]() {
    while (!SherpaOnnxVoiceActivityDetectorEmpty(vad)) {
      const SherpaOnnxSpeechSegment* seg = SherpaOnnxVoiceActivityDetectorFront(vad);
      const auto t0 = Clock::now();
      const SherpaOnnxOfflineStream* stream = SherpaOnnxCreateOfflineStream(recognizer);
      SherpaOnnxAcceptWaveformOffline(stream, wave->sample_rate, seg->samples, seg->n);
      SherpaOnnxDecodeOfflineStream(recognizer, stream);
      const SherpaOnnxOfflineRecognizerResult* r = SherpaOnnxGetOfflineStreamResult(stream);
      compute += Since(t0);
      if (r->text && r->text[0] != '\0') {
        ++phrases;
        text += (text.empty() ? "" : " ") + std::string(r->text);
      }
      SherpaOnnxDestroyOfflineRecognizerResult(r);
      SherpaOnnxDestroyOfflineStream(stream);
      SherpaOnnxDestroySpeechSegment(seg);
      SherpaOnnxVoiceActivityDetectorPop(vad);
    }
  };

  const int32_t step = wave->sample_rate / 10;  // 100 ms
  std::vector<float> pending;
  for (int32_t i = 0; i < wave->num_samples; i += step) {
    const int32_t n = std::min(step, wave->num_samples - i);
    const auto t0 = Clock::now();
    pending.insert(pending.end(), wave->samples + i, wave->samples + i + n);
    while (pending.size() >= static_cast<size_t>(window)) {
      SherpaOnnxVoiceActivityDetectorAcceptWaveform(vad, pending.data(), window);
      pending.erase(pending.begin(), pending.begin() + window);
    }
    compute += Since(t0);
    drain();
  }
  SherpaOnnxVoiceActivityDetectorFlush(vad);
  drain();

  const double duration = static_cast<double>(wave->num_samples) / wave->sample_rate;
  std::cout << "text: " << text << "\n";
  std::printf("phrases %d  duration %.1fs  compute %.2fs  RTF %.3f\n", phrases, duration, compute,
              compute / duration);

  SherpaOnnxFreeWave(wave);
  SherpaOnnxDestroyVoiceActivityDetector(vad);
  SherpaOnnxDestroyOfflineRecognizer(recognizer);
  return 0;
}
