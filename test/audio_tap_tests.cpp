#include "audio_sink.h"

#include <atomic>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using pcm::video::AudioSink;
using pcm::video::AudioSinkSlot;
using pcm::video::AudioTap;

namespace {
class NullSink final : public AudioSink {
public:
  void onAudio(const QString &, const int16_t *, std::size_t, int) override {}
};
} // namespace

TEST(AudioTapTest, TapIsSilentWithoutCallback) {
  AudioTap tap;
  const std::vector<int16_t> samples{1, 2, 3};
  tap.push(samples.data(), samples.size(), 48000);
  std::vector<int16_t> received;
  tap.setCallback([&](const int16_t *s, std::size_t n, int) { received.assign(s, s + n); });
  EXPECT_TRUE(received.empty()); // earlier pushes are not replayed
  const std::vector<int16_t> later{9, 8};
  tap.push(later.data(), later.size(), 48000);
  EXPECT_EQ(received, later);
}

TEST(AudioTapTest, TapDeliversSamplesToCallback) {
  AudioTap tap;
  std::vector<int16_t> received;
  int receivedRate = 0;
  int calls = 0;
  tap.setCallback([&](const int16_t *s, std::size_t n, int rate) {
    received.assign(s, s + n);
    receivedRate = rate;
    ++calls;
  });
  const std::vector<int16_t> samples{4, 5, 6, 7};
  tap.push(samples.data(), samples.size(), 16000);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(received, samples);
  EXPECT_EQ(receivedRate, 16000);
}

TEST(AudioTapTest, DisabledTapDeliversNothing) {
  AudioTap tap;
  int calls = 0;
  tap.setCallback([&](const int16_t *, std::size_t, int) { ++calls; });
  const std::vector<int16_t> samples{1};
  tap.setEnabled(false);
  tap.push(samples.data(), samples.size(), 48000);
  EXPECT_EQ(calls, 0);
  tap.setEnabled(true);
  tap.push(samples.data(), samples.size(), 48000);
  EXPECT_EQ(calls, 1);
}

TEST(AudioSinkSlotTest, SlotReturnsNullUntilSet) {
  AudioSinkSlot slot;
  EXPECT_EQ(slot.get(), nullptr);
}

TEST(AudioSinkSlotTest, SlotHoldsSink) {
  AudioSinkSlot slot;
  auto sink = std::make_shared<NullSink>();
  slot.set(sink);
  EXPECT_EQ(slot.get(), sink);
  slot.set(nullptr);
  EXPECT_EQ(slot.get(), nullptr);
}

TEST(AudioTapTest, TapSurvivesConcurrentSetCallback) {
  // Full value of this test is under TSAN; without it, it checks callback content and liveness.
  AudioTap tap;
  std::atomic<long> calls{0};
  std::atomic<long> bad{0};
  std::atomic<bool> done{false};
  std::thread toggler([&] {
    while (!done) {
      tap.setCallback([&](const int16_t *s, std::size_t n, int) {
        for (std::size_t i = 0; i < n; ++i) {
          if (s[i] != 1) {
            ++bad;
          }
        }
        ++calls;
      });
      tap.setCallback({});
      std::this_thread::yield();
    }
  });
  std::thread muter([&] {
    bool on = false;
    while (!done) {
      tap.setEnabled(on = !on);
      std::this_thread::yield();
    }
    tap.setEnabled(true);
  });
  const std::vector<int16_t> samples(160, 1);
  for (int i = 0; i < 200000 && (i < 10000 || calls == 0); ++i) {
    tap.push(samples.data(), samples.size(), 48000);
  }
  done = true;
  toggler.join();
  muter.join();
  EXPECT_GT(calls.load(), 0);
  EXPECT_EQ(bad.load(), 0);
}

TEST(AudioTapTest, ActiveRequiresCallbackEnabledAndSinkActive) {
  AudioTap tap;
  EXPECT_FALSE(tap.active()); // no callback
  tap.setCallback([](const int16_t *, std::size_t, int) {});
  EXPECT_TRUE(tap.active());
  tap.setEnabled(false);
  EXPECT_FALSE(tap.active());
  tap.setEnabled(true);
  tap.setSinkActive(false);
  EXPECT_FALSE(tap.active());
  tap.setSinkActive(true);
  EXPECT_TRUE(tap.active());
  tap.setCallback({});
  EXPECT_FALSE(tap.active());
}

TEST(AudioTapTest, InactiveSinkDeliversNothing) {
  AudioTap tap;
  int calls = 0;
  tap.setCallback([&](const int16_t *, std::size_t, int) { ++calls; });
  const std::vector<int16_t> samples{1};
  tap.setSinkActive(false);
  tap.push(samples.data(), samples.size(), 48000);
  EXPECT_EQ(calls, 0);
  tap.setSinkActive(true);
  tap.push(samples.data(), samples.size(), 48000);
  EXPECT_EQ(calls, 1);
}

TEST(AudioTapTest, SinkFlagNeverReenablesMutedTap) {
  AudioTap tap;
  int calls = 0;
  tap.setCallback([&](const int16_t *, std::size_t, int) { ++calls; });
  const std::vector<int16_t> samples{1};
  tap.setEnabled(false);
  tap.setSinkActive(false);
  tap.setSinkActive(true);
  tap.push(samples.data(), samples.size(), 48000);
  EXPECT_EQ(calls, 0);
  EXPECT_FALSE(tap.active());
  // And muting does not disturb the sink flag.
  tap.setEnabled(true);
  EXPECT_TRUE(tap.active());
}

TEST(AudioSinkSlotTest, HasSinkTracksSet) {
  AudioSinkSlot slot;
  EXPECT_FALSE(slot.hasSink());
  slot.set(std::make_shared<NullSink>());
  EXPECT_TRUE(slot.hasSink());
  slot.set(nullptr);
  EXPECT_FALSE(slot.hasSink());
}

TEST(DownmixTest, StereoAveragesFrames) {
  const std::vector<int16_t> in{10, 20, 30, 50};
  std::vector<int16_t> out;
  pcm::video::downmixToMono(in.data(), 2, 2, out);
  EXPECT_EQ(out, (std::vector<int16_t>{15, 40}));
}

TEST(DownmixTest, NegativeValuesAverage) {
  const std::vector<int16_t> in{-10, -20, -32768, -32768, 100, -300};
  std::vector<int16_t> out;
  pcm::video::downmixToMono(in.data(), 3, 2, out);
  EXPECT_EQ(out, (std::vector<int16_t>{-15, -32768, -100}));
}

TEST(DownmixTest, MonoPassesThrough) {
  const std::vector<int16_t> in{1, -2, 3};
  std::vector<int16_t> out{7, 7, 7, 7};
  pcm::video::downmixToMono(in.data(), in.size(), 1, out);
  EXPECT_EQ(out, in);
}

TEST(DownmixTest, TrailingPartialFrameIgnored) {
  // 5 samples at 2 channels = 2 whole frames; the caller passes frames = n / channels.
  const std::vector<int16_t> in{2, 4, 6, 8, 99};
  std::vector<int16_t> out;
  pcm::video::downmixToMono(in.data(), in.size() / 2, 2, out);
  EXPECT_EQ(out, (std::vector<int16_t>{3, 7}));
}
