#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "model_locator.h"
#include "sherpa-onnx/c-api/cxx-api.h"
#include "sherpa_backend.h"
#include "transcription_engine.h"

namespace fs = std::filesystem;
using namespace pcm::transcription;
using namespace std::chrono_literals;

namespace {

std::optional<ModelPaths> models() {
  const char* env = std::getenv("SESSIO_MODELS_DIR");
  return locateModels(fs::current_path(), currentPlatform(), env ? env : "").paths;
}

std::vector<int16_t> loadSample() {
  const char* env = std::getenv("SESSIO_MODELS_DIR");
  const fs::path wav = fs::path(env ? env : "") / "gigaam-v3-rnnt" / "test_wavs" / "example.wav";
  auto wave = sherpa_onnx::cxx::ReadWave(wav.string());
  std::vector<int16_t> out(wave.samples.size());
  for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<int16_t>(wave.samples[i] * 32767.0f);
  return out;  // 16 kHz
}

bool hasCyrillic(const std::string& text) {
  for (size_t i = 0; i + 1 < text.size(); ++i) {
    const auto c = static_cast<unsigned char>(text[i]);
    if (c == 0xD0 || c == 0xD1) return true;
  }
  return false;
}

struct Lag {
  std::mutex mutex;
  std::vector<double> seconds;
  std::vector<TranscribedPhrase> phrases;
};

double percentile(std::vector<double> v, double p) {
  std::sort(v.begin(), v.end());
  return v[std::min(v.size() - 1, static_cast<size_t>(p * static_cast<double>(v.size())))];
}

// Feeds `tracks` (silence for everyone except `speakers`) in real time, 100 ms at
// a time, and records how late each phrase arrives after its audio ended.
std::vector<double> runRealtime(const ModelPaths& paths, int tracks, int speakers,
                                const std::vector<int16_t>& sample, Lag& lag) {
  EngineCallbacks cb;
  const auto start = std::chrono::steady_clock::now();
  cb.on_phrase = [&](const TranscribedPhrase& p) {
    const double audio_end = static_cast<double>(p.end_ms) / 1000.0;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::lock_guard lock(lag.mutex);
    lag.seconds.push_back(now - audio_end);
    lag.phrases.push_back(p);
  };
  TranscriptionEngine engine(makeSherpaVadFactory(paths), makeSherpaRecognizer(paths), cb);
  for (int t = 0; t < tracks; ++t) {
    engine.addTrack({"t" + std::to_string(t), t == 0 ? TrackRole::Practitioner : TrackRole::Participant,
                     "N" + std::to_string(t)}, 0);
  }
  const size_t step = 1600;  // 100 ms at 16 kHz
  const std::vector<int16_t> quiet(step, 0);
  const size_t total = sample.size() + 16000 * 2;  // two seconds of tail
  for (size_t pos = 0; pos < total; pos += step) {
    for (int t = 0; t < tracks; ++t) {
      const std::string id = "t" + std::to_string(t);
      if (t < speakers && pos < sample.size()) {
        const size_t n = std::min(step, sample.size() - pos);
        engine.pushAudio(id, sample.data() + pos, n, 16000);
      } else {
        engine.pushAudio(id, quiet.data(), step, 16000);
      }
    }
    std::this_thread::sleep_until(start + std::chrono::milliseconds((pos + step) / 16));
  }
  engine.stop(10s);
  std::lock_guard lock(lag.mutex);
  return lag.seconds;
}

}  // namespace

TEST(TranscriptionModelTest, RecognisesRussianSample) {
  const auto paths = models();
  if (!paths) GTEST_SKIP() << "models not installed; set SESSIO_MODELS_DIR";
  auto recognizer = makeSherpaRecognizer(*paths);
  const auto sample = loadSample();
  std::vector<float> floats(sample.size());
  for (size_t i = 0; i < sample.size(); ++i) floats[i] = static_cast<float>(sample[i]) / 32768.0f;
  const std::string text = recognizer->transcribe(floats);
  EXPECT_FALSE(text.empty());
  EXPECT_TRUE(hasCyrillic(text)) << "text had no Cyrillic letters";
}

TEST(TranscriptionModelTest, EngineProducesTimedPhrasesFromSample) {
  const auto paths = models();
  if (!paths) GTEST_SKIP() << "models not installed; set SESSIO_MODELS_DIR";
  const auto sample = loadSample();
  std::mutex m;
  std::vector<TranscribedPhrase> phrases;
  EngineCallbacks cb;
  cb.on_phrase = [&](const TranscribedPhrase& p) {
    std::lock_guard lock(m);
    phrases.push_back(p);
  };
  TranscriptionEngine engine(makeSherpaVadFactory(*paths), makeSherpaRecognizer(*paths), cb);
  engine.addTrack({"a", TrackRole::Participant, "A"}, 5000);
  engine.pushAudio("a", sample.data(), sample.size(), 16000);
  engine.removeTrack("a");
  engine.stop(15s);
  ASSERT_FALSE(phrases.empty());
  for (const auto& p : phrases) {
    EXPECT_GE(p.start_ms, 5000);
    EXPECT_GT(p.end_ms, p.start_ms);
    EXPECT_TRUE(hasCyrillic(p.text));
  }
}

TEST(TranscriptionModelTest, TwoSpeakersAmongTenTracksStayWithinOneSecond) {
  const auto paths = models();
  if (!paths) GTEST_SKIP() << "models not installed; set SESSIO_MODELS_DIR";
  if (!std::getenv("SESSIO_LOADTEST")) GTEST_SKIP() << "set SESSIO_LOADTEST=1 (runs in real time)";
  Lag lag;
  const auto lags = runRealtime(*paths, 10, 2, loadSample(), lag);
  ASSERT_GE(lags.size(), 2u);
  const double p95 = percentile(lags, 0.95);
  std::string all;
  for (double l : lags) all += std::to_string(l) + " ";
  std::cout << "[loadtest] phrases=" << lags.size() << " p95=" << p95 << " lags: " << all << std::endl;
  EXPECT_LE(p95, 1.0);
}
