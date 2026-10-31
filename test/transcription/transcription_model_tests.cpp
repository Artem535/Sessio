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

struct PhraseLag {
  TranscribedPhrase phrase;
  double lag_s;
};

struct RunResult {
  std::vector<PhraseLag> phrases;
  EngineStats stats;
};

double durationS(const TranscribedPhrase& p) { return static_cast<double>(p.end_ms - p.start_ms) / 1000.0; }

// Feeds `tracks` in real time, 100 ms at a time. Track i < speakers_start_s.size() plays
// the sample starting `speakers_start_s[i]` seconds into the stream (silence before that);
// every other track is silent. Engine start offsets stay 0, so end_ms already contains the
// speaker's delay and lag = wall time of delivery - end_ms.
RunResult runRealtime(const ModelPaths& paths, int tracks, const std::vector<double>& speakers_start_s,
                      const std::vector<int16_t>& sample) {
  RunResult result;
  std::mutex mutex;
  EngineCallbacks cb;
  const auto start = std::chrono::steady_clock::now();
  cb.on_phrase = [&](const TranscribedPhrase& p) {
    const double audio_end = static_cast<double>(p.end_ms) / 1000.0;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::lock_guard lock(mutex);
    result.phrases.push_back({p, now - audio_end});
  };
  TranscriptionEngine engine(makeSherpaVadFactory(paths), makeSherpaRecognizer(paths), cb);
  for (int t = 0; t < tracks; ++t) {
    engine.addTrack({"t" + std::to_string(t), t == 0 ? TrackRole::Practitioner : TrackRole::Participant,
                     "N" + std::to_string(t)}, 0);
  }
  const size_t step = 1600;  // 100 ms at 16 kHz
  const std::vector<int16_t> quiet(step, 0);
  double last_end_s = 0;
  for (double s : speakers_start_s) last_end_s = std::max(last_end_s, s);
  const size_t total = static_cast<size_t>(last_end_s * 16000) + sample.size() + 16000 * 2;  // two seconds of tail
  for (size_t pos = 0; pos < total; pos += step) {
    for (int t = 0; t < tracks; ++t) {
      const std::string id = "t" + std::to_string(t);
      bool fed = false;
      if (static_cast<size_t>(t) < speakers_start_s.size()) {
        const size_t begin = static_cast<size_t>(speakers_start_s[static_cast<size_t>(t)] * 16000);
        if (pos >= begin && pos - begin < sample.size()) {
          const size_t n = std::min(step, sample.size() - (pos - begin));
          engine.pushAudio(id, sample.data() + (pos - begin), n, 16000);
          fed = true;
        }
      }
      if (!fed) engine.pushAudio(id, quiet.data(), step, 16000);
    }
    std::this_thread::sleep_until(start + std::chrono::milliseconds((pos + step) / 16));
  }
  engine.stop(10s);
  result.stats = engine.stats();
  std::lock_guard lock(mutex);
  return result;
}

void printPhrases(const RunResult& r) {
  for (const auto& pl : r.phrases) {
    std::cout << "[loadtest] track=" << pl.phrase.track_id << " duration=" << durationS(pl.phrase)
              << " s lag=" << pl.lag_s << " s" << std::endl;
  }
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

TEST(TranscriptionModelTest, VadRetainsAudioBeforeAndAfterSpeechBoundaries) {
  const auto paths = models();
  if (!paths) GTEST_SKIP() << "models not installed";
  std::vector<float> audio(16000, 0.0f);
  for (auto sample : loadSample()) audio.push_back(sample / 32768.0f);
  audio.resize(audio.size() + 16000, 0.0f);
  sherpa_onnx::cxx::VadModelConfig config;
  config.silero_vad.model = paths->vad.string();
  config.silero_vad.threshold = 0.5F;
  config.silero_vad.min_silence_duration = 0.8F;
  config.silero_vad.min_speech_duration = 0.25F;
  config.silero_vad.max_speech_duration = 20.0F;
  config.silero_vad.window_size = 512;
  config.sample_rate = 16000;
  auto reference = sherpa_onnx::cxx::VoiceActivityDetector::Create(config, 120.0F);
  auto actual = makeSherpaVadFactory(*paths)();
  std::vector<SpeechSegment> segments;
  for (size_t offset = 0; offset + 512 <= audio.size(); offset += 512) {
    reference.AcceptWaveform(audio.data() + offset, 512);
    actual->accept(audio.data() + offset, 512);
    while (actual->hasSegment()) segments.push_back(actual->popSegment());
  }
  reference.Flush(); actual->flush();
  while (actual->hasSegment()) segments.push_back(actual->popSegment());
  ASSERT_FALSE(segments.empty());
  EXPECT_EQ(segments.size(), 1u); // the source's short internal pause stays within one phrase
  ASSERT_FALSE(reference.IsEmpty());
  const auto raw = reference.Front();
  EXPECT_LE(segments.front().start_sample, raw.start - 1600); // at least 100 ms of leading context
  EXPECT_GE(segments.front().start_sample + segments.front().samples.size(),
            raw.start + raw.samples.size() + 2400); // at least 150 ms of trailing context
  const auto &first = segments.front();
  ASSERT_GE(first.start_sample, 0);
  ASSERT_LE(first.start_sample + first.samples.size(), audio.size());
  EXPECT_TRUE(std::equal(first.samples.begin(), first.samples.end(), audio.begin() + first.start_sample));
  for (size_t i = 1; i < segments.size(); ++i)
    EXPECT_GE(segments[i].start_sample, segments[i - 1].start_sample + segments[i - 1].samples.size());
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

TEST(TranscriptionModelTest, TwoStaggeredSpeakersAmongTenTracksKeepLagProportionalToPhraseLength) {
  const auto paths = models();
  if (!paths) GTEST_SKIP() << "models not installed; set SESSIO_MODELS_DIR";
  if (!std::getenv("SESSIO_LOADTEST")) GTEST_SKIP() << "set SESSIO_LOADTEST=1 (runs in real time)";
  const auto result = runRealtime(*paths, 10, {0.0, 4.0}, loadSample());
  printPhrases(result);
  ASSERT_GE(result.phrases.size(), 2u);  // at least one phrase per speaker
  for (const auto& pl : result.phrases) {
    EXPECT_LE(pl.lag_s, 0.8 + 0.15 * durationS(pl.phrase) + 0.2)
        << "track " << pl.phrase.track_id << " phrase of " << durationS(pl.phrase) << " s";
  }
  EXPECT_EQ(result.stats.queued, 0u);
  EXPECT_EQ(result.stats.dropped_on_stop, 0u);
}

// Worst case, documented without a tight bound: two identical long phrases that end at
// the same instant are decoded one after the other by design (one shared recogniser), so
// the second one waits for the first. This only guards against unbounded growth.
TEST(TranscriptionModelTest, TwoSimultaneousSpeakersAmongTenTracksDrainWithinThreeSeconds) {
  const auto paths = models();
  if (!paths) GTEST_SKIP() << "models not installed; set SESSIO_MODELS_DIR";
  if (!std::getenv("SESSIO_LOADTEST")) GTEST_SKIP() << "set SESSIO_LOADTEST=1 (runs in real time)";
  const auto result = runRealtime(*paths, 10, {0.0, 0.0}, loadSample());
  printPhrases(result);
  ASSERT_GE(result.phrases.size(), 4u);
  for (const auto& pl : result.phrases) EXPECT_LE(pl.lag_s, 3.0);
  EXPECT_EQ(result.stats.queued, 0u);
  EXPECT_EQ(result.stats.dropped_on_stop, 0u);
}
