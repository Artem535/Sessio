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
  SUCCEED();
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
  AudioTap tap;
  std::atomic<long> calls{0};
  std::atomic<bool> done{false};
  std::thread toggler([&] {
    while (!done) {
      tap.setCallback([&](const int16_t *, std::size_t, int) { ++calls; });
      tap.setCallback({});
    }
  });
  const std::vector<int16_t> samples(160, 1);
  for (int i = 0; i < 10000; ++i)
    tap.push(samples.data(), samples.size(), 48000);
  done = true;
  toggler.join();
  SUCCEED();
}
