#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "transcription_engine.h"
#include "transcription_test_support.h"

using namespace pcm::transcription;
using namespace pcm::transcription::testing;
using namespace std::chrono_literals;

namespace {

struct Collector {
  void add(const TranscribedPhrase& p) {
    std::lock_guard lock(mutex);
    phrases.push_back(p);
  }
  size_t size() {
    std::lock_guard lock(mutex);
    return phrases.size();
  }
  std::vector<TranscribedPhrase> snapshot() {
    std::lock_guard lock(mutex);
    return phrases;
  }
  std::mutex mutex;
  std::vector<TranscribedPhrase> phrases;
  std::mutex delayed_mutex;
  std::vector<bool> delayed;
  void addDelayed(bool d) {
    std::lock_guard lock(delayed_mutex);
    delayed.push_back(d);
  }
  bool sawDelayed(bool value) {
    std::lock_guard lock(delayed_mutex);
    for (bool d : delayed) if (d == value) return true;
    return false;
  }
};

std::unique_ptr<TranscriptionEngine> makeEngine(Collector& c, std::shared_ptr<ISpeechRecognizer> rec,
                                                EngineConfig config = {}) {
  EngineCallbacks cb;
  cb.on_phrase = [&c](const TranscribedPhrase& p) { c.add(p); };
  cb.on_delayed = [&c](bool d) { c.addDelayed(d); };
  return std::make_unique<TranscriptionEngine>(
      [] { return std::make_unique<FakeVad>(); }, std::move(rec), cb, config);
}

void push(TranscriptionEngine& e, const TrackId& id, const std::vector<int16_t>& audio) {
  e.pushAudio(id, audio.data(), audio.size(), 48000);
}

class ThrowingVad : public FakeVad {
 public:
  void accept(const float*, size_t) override { throw std::runtime_error("vad boom"); }
};

}  // namespace

TEST(TranscriptionEngineTest, EmitsPhraseWithRoleNameAndTimes) {
  Collector c;
  auto engine = makeEngine(c, std::make_shared<FakeRecognizer>());
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 1000);
  push(*engine, "alice", concat({silence(500), speech(1000), silence(800)}));

  ASSERT_TRUE(waitFor([&] { return c.size() == 1; }));
  const auto p = c.snapshot().front();
  EXPECT_EQ(p.track_id, "alice");
  EXPECT_EQ(p.role, TrackRole::Participant);
  EXPECT_EQ(p.speaker_name, "Alice");
  EXPECT_EQ(p.text, "phrase 0");
  EXPECT_NEAR(static_cast<double>(p.start_ms), 1500.0, 64.0);
  EXPECT_NEAR(static_cast<double>(p.end_ms), 2500.0, 64.0);
}

TEST(TranscriptionEngineTest, SeparatesTracksAndKeepsDecodeOrder) {
  Collector c;
  auto engine = makeEngine(c, std::make_shared<FakeRecognizer>());
  engine->addTrack({"me", TrackRole::Practitioner, "Dr"}, 0);
  engine->addTrack({"client", TrackRole::Participant, "C"}, 0);
  push(*engine, "me", concat({speech(500), silence(600)}));
  push(*engine, "client", concat({speech(500), silence(600)}));

  ASSERT_TRUE(waitFor([&] { return c.size() == 2; }));
  const auto phrases = c.snapshot();
  EXPECT_NE(phrases[0].track_id, phrases[1].track_id);
  EXPECT_EQ(phrases[0].text, "phrase 0");  // one worker decodes in queue order
  EXPECT_EQ(phrases[1].text, "phrase 1");
}

TEST(TranscriptionEngineTest, RemoveTrackFlushesOpenPhrase) {
  Collector c;
  auto engine = makeEngine(c, std::make_shared<FakeRecognizer>());
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  push(*engine, "alice", speech(1000));  // no trailing silence: phrase still open
  engine->removeTrack("alice");
  ASSERT_TRUE(waitFor([&] { return c.size() == 1; }));
}

TEST(TranscriptionEngineTest, ResetSeparatesSpeechAndPreservesRemoteTrack) {
  Collector c;
  auto engine = makeEngine(c, std::make_shared<FakeRecognizer>());
  engine->addTrack({"local", TrackRole::Practitioner, "Local"}, 0);
  engine->addTrack({"remote", TrackRole::Participant, "Remote"}, 0);
  push(*engine, "local", speech(500));
  push(*engine, "remote", speech(500));
  engine->resetTrack("local", 20000);
  push(*engine, "local", concat({speech(500), silence(200)}));
  push(*engine, "remote", concat({speech(500), silence(200)}));
  engine->stop(2s);
  const auto phrases = c.snapshot();
  ASSERT_EQ(phrases.size(), 3u);
  std::vector<TranscribedPhrase> local;
  std::vector<TranscribedPhrase> remote;
  for (const auto &phrase : phrases)
    (phrase.track_id == "local" ? local : remote).push_back(phrase);
  ASSERT_EQ(local.size(), 2u);
  EXPECT_LT(local[0].end_ms, 20000);
  EXPECT_GE(local[1].start_ms, 20000);
  ASSERT_EQ(remote.size(), 1u);
  EXPECT_GT(remote[0].end_ms - remote[0].start_ms, 900);
}

TEST(TranscriptionEngineTest, ResetAcceptsReplacementSampleRateAndKeepsIdentity) {
  Collector c;
  auto engine = makeEngine(c, std::make_shared<FakeRecognizer>());
  engine->addTrack({"local", TrackRole::Practitioner, "Local"}, 500);
  push(*engine, "local", speech(500));
  engine->resetTrack("local", 10000);
  std::vector<int16_t> replacement(8000, 16000);
  replacement.resize(11200, 0);
  engine->pushAudio("local", replacement.data(), replacement.size(), 16000);
  engine->stop(2s);
  const auto phrases = c.snapshot();
  ASSERT_EQ(phrases.size(), 2u);
  EXPECT_EQ(phrases[1].track_id, "local");
  EXPECT_EQ(phrases[1].speaker_name, "Local");
  EXPECT_GE(phrases[1].start_ms, 10000);
}

TEST(TranscriptionEngineTest, IgnoresUnknownAndRemovedTracks) {
  Collector c;
  auto engine = makeEngine(c, std::make_shared<FakeRecognizer>());
  push(*engine, "nobody", speech(500));
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  engine->removeTrack("alice");
  push(*engine, "alice", concat({speech(500), silence(600)}));
  std::this_thread::sleep_for(150ms);
  EXPECT_EQ(c.size(), 0u);
}

TEST(TranscriptionEngineTest, StopDrainsQueuedPhrases) {
  Collector c;
  auto rec = std::make_shared<FakeRecognizer>([](int, size_t) {
    std::this_thread::sleep_for(20ms);
    return std::string("ok");
  });
  auto engine = makeEngine(c, rec);
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  for (int i = 0; i < 5; ++i) push(*engine, "alice", concat({speech(200), silence(200)}));
  engine->stop(2s);
  EXPECT_EQ(c.size(), 5u);
  EXPECT_EQ(engine->stats().dropped_on_stop, 0u);
}

TEST(TranscriptionEngineTest, StopWithZeroTimeoutCountsDroppedPhrases) {
  Collector c;
  auto rec = std::make_shared<FakeRecognizer>([](int, size_t) {
    std::this_thread::sleep_for(100ms);
    return std::string("slow");
  });
  auto engine = makeEngine(c, rec);
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  for (int i = 0; i < 6; ++i) push(*engine, "alice", concat({speech(200), silence(200)}));
  engine->stop(0ms);
  const auto stats = engine->stats();
  EXPECT_EQ(c.size() + stats.dropped_on_stop, 6u);
  EXPECT_GT(stats.dropped_on_stop, 0u);
}

TEST(TranscriptionEngineTest, DecodeFailureIsCountedAndSessionContinues) {
  Collector c;
  auto rec = std::make_shared<FakeRecognizer>([](int call, size_t) -> std::string {
    if (call == 1) throw std::runtime_error("boom");
    return "ok";
  });
  auto engine = makeEngine(c, rec);
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  for (int i = 0; i < 3; ++i) push(*engine, "alice", concat({speech(300), silence(300)}));
  engine->stop(2s);
  EXPECT_EQ(c.size(), 2u);
  EXPECT_EQ(engine->stats().decode_failures, 1u);
}

TEST(TranscriptionEngineTest, EmptyTextIsNotEmitted) {
  Collector c;
  auto rec = std::make_shared<FakeRecognizer>([](int, size_t) { return std::string("  "); });
  auto engine = makeEngine(c, rec);
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  push(*engine, "alice", concat({speech(300), silence(300)}));
  engine->stop(2s);
  EXPECT_EQ(c.size(), 0u);
}

TEST(TranscriptionEngineTest, ReportsDelayWhenQueueFallsBehindAndRecovers) {
  Collector c;
  auto rec = std::make_shared<FakeRecognizer>([](int, size_t) {
    std::this_thread::sleep_for(120ms);
    return std::string("slow");
  });
  EngineConfig config;
  config.delayed_after = 50ms;
  auto engine = makeEngine(c, rec, config);
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  for (int i = 0; i < 3; ++i) push(*engine, "alice", concat({speech(200), silence(200)}));
  ASSERT_TRUE(waitFor([&] { return c.size() == 3; }));
  EXPECT_TRUE(c.sawDelayed(true));

  push(*engine, "alice", concat({speech(200), silence(200)}));
  ASSERT_TRUE(waitFor([&] { return c.size() == 4; }));
  EXPECT_TRUE(c.sawDelayed(false));
  EXPECT_FALSE(engine->stats().delayed);
}

TEST(TranscriptionEngineTest, TenTracksTwoSpeakingAllPhrasesArriveAndQueueDrains) {
  Collector c;
  auto engine = makeEngine(c, std::make_shared<FakeRecognizer>());
  for (int i = 0; i < 10; ++i) {
    engine->addTrack({"t" + std::to_string(i),
                      i == 0 ? TrackRole::Practitioner : TrackRole::Participant,
                      "N" + std::to_string(i)}, 0);
  }
  for (int round = 0; round < 3; ++round) {
    for (int i = 0; i < 10; ++i) {
      const bool speaks = i < 2;
      push(*engine, "t" + std::to_string(i),
           speaks ? concat({speech(300), silence(300)}) : silence(600));
    }
  }
  engine->stop(2s);
  EXPECT_EQ(c.size(), 6u);
  EXPECT_EQ(engine->stats().queued, 0u);
}

TEST(TranscriptionEngineTest, PushAfterStopIsIgnored) {
  Collector c;
  auto engine = makeEngine(c, std::make_shared<FakeRecognizer>());
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  engine->stop(1s);
  push(*engine, "alice", concat({speech(300), silence(300)}));
  std::this_thread::sleep_for(100ms);
  EXPECT_EQ(c.size(), 0u);
}

TEST(TranscriptionEngineTest, ThrowingPhraseCallbackKeepsEngineRunningAndIsNotADecodeFailure) {
  std::atomic<int> calls{0};
  EngineCallbacks cb;
  cb.on_phrase = [&](const TranscribedPhrase&) {
    ++calls;
    throw std::runtime_error("callback boom");
  };
  TranscriptionEngine engine([] { return std::make_unique<FakeVad>(); },
                             std::make_shared<FakeRecognizer>(), cb);
  engine.addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  for (int i = 0; i < 3; ++i) push(engine, "alice", concat({speech(300), silence(300)}));
  engine.stop(2s);
  EXPECT_EQ(calls.load(), 3);
  EXPECT_EQ(engine.stats().decode_failures, 0u);
  EXPECT_EQ(engine.stats().phrases, 0u);
  EXPECT_EQ(engine.stats().callback_failures, 3u);
}

TEST(TranscriptionEngineTest, DelayedFlagClearsWhenQueueDrainsWithoutNewPhrase) {
  Collector c;
  auto rec = std::make_shared<FakeRecognizer>([](int, size_t) {
    std::this_thread::sleep_for(120ms);
    return std::string("slow");
  });
  EngineConfig config;
  config.delayed_after = 50ms;
  auto engine = makeEngine(c, rec, config);
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  for (int i = 0; i < 3; ++i) push(*engine, "alice", concat({speech(200), silence(200)}));
  ASSERT_TRUE(waitFor([&] { return c.sawDelayed(true); }));
  ASSERT_TRUE(waitFor([&] { return c.size() == 3; }));
  ASSERT_TRUE(waitFor([&] { return c.sawDelayed(false); }));
  EXPECT_FALSE(engine->stats().delayed);
}

TEST(TranscriptionEngineTest, ThrowingDelayedCallbackDoesNotCrashAndPhrasesStillArrive) {
  Collector c;
  auto rec = std::make_shared<FakeRecognizer>([](int, size_t) {
    std::this_thread::sleep_for(120ms);
    return std::string("slow");
  });
  EngineConfig config;
  config.delayed_after = 50ms;
  EngineCallbacks cb;
  cb.on_phrase = [&c](const TranscribedPhrase& p) { c.add(p); };
  std::atomic<int> delayed_calls{0};
  cb.on_delayed = [&delayed_calls](bool) {
    ++delayed_calls;
    throw std::runtime_error("delayed boom");
  };
  TranscriptionEngine engine([] { return std::make_unique<FakeVad>(); }, rec, cb, config);
  engine.addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  for (int i = 0; i < 3; ++i) push(engine, "alice", concat({speech(200), silence(200)}));
  ASSERT_TRUE(waitFor([&] { return c.size() == 3; }));
  ASSERT_TRUE(waitFor([&] { return delayed_calls.load() > 0; }));
  ASSERT_TRUE(waitFor([&] { return engine.stats().callback_failures > 0; }));
}

TEST(TranscriptionEngineTest, FailingTrackIsIsolatedAndCounted) {
  Collector c;
  EngineCallbacks cb;
  cb.on_phrase = [&c](const TranscribedPhrase& p) { c.add(p); };
  std::atomic<int> made{0};
  TranscriptionEngine engine(
      [&]() -> std::unique_ptr<IVoiceActivityDetector> {
        if (made++ == 0) return std::make_unique<ThrowingVad>();
        return std::make_unique<FakeVad>();
      },
      std::make_shared<FakeRecognizer>(), cb);
  engine.addTrack({"bad", TrackRole::Participant, "Bad"}, 0);
  engine.addTrack({"good", TrackRole::Participant, "Good"}, 0);
  push(engine, "bad", concat({speech(500), silence(600)}));
  push(engine, "bad", concat({speech(500), silence(600)}));
  push(engine, "good", concat({speech(500), silence(600)}));
  ASSERT_TRUE(waitFor([&] { return c.size() == 1; }));
  engine.stop(2s);
  EXPECT_EQ(c.snapshot().front().track_id, "good");
  EXPECT_EQ(engine.stats().track_failures, 1u);
}

TEST(TranscriptionEngineTest, NonStdExceptionFromRecogniserIsCountedAsFailure) {
  Collector c;
  auto rec = std::make_shared<FakeRecognizer>([](int call, size_t) -> std::string {
    if (call == 0) throw 42;
    return "ok";
  });
  auto engine = makeEngine(c, rec);
  engine->addTrack({"alice", TrackRole::Participant, "Alice"}, 0);
  for (int i = 0; i < 2; ++i) push(*engine, "alice", concat({speech(300), silence(300)}));
  engine->stop(2s);
  EXPECT_EQ(engine->stats().decode_failures, 1u);
  EXPECT_EQ(c.size(), 1u);
}
