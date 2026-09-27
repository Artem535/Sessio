// spikes/livekit-cpp-spike/test/audio_chunker_tests.cpp
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "audio_chunker.h"

using pcm::spike::AudioChunker;

TEST(AudioChunkerTest, EmitsNoFramesBelowThreshold) {
  AudioChunker chunker(/*samplesPerChannel=*/4, /*channels=*/1);
  std::vector<int16_t> samples = {1, 2, 3};
  const auto frames = chunker.push(samples);
  EXPECT_TRUE(frames.empty());
}

TEST(AudioChunkerTest, EmitsOneFrameExactly) {
  AudioChunker chunker(/*samplesPerChannel=*/4, /*channels=*/1);
  std::vector<int16_t> samples = {1, 2, 3, 4};
  const auto frames = chunker.push(samples);
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0], (std::vector<int16_t>{1, 2, 3, 4}));
}

TEST(AudioChunkerTest, CarriesRemainderAcrossCalls) {
  AudioChunker chunker(/*samplesPerChannel=*/4, /*channels=*/1);
  EXPECT_TRUE(chunker.push({1, 2, 3}).empty());
  const auto frames = chunker.push({4, 5, 6, 7});
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0], (std::vector<int16_t>{1, 2, 3, 4}));
  const auto frames2 = chunker.push({});
  EXPECT_TRUE(frames2.empty());
}

TEST(AudioChunkerTest, EmitsMultipleFramesFromOnePush) {
  AudioChunker chunker(/*samplesPerChannel=*/2, /*channels=*/1);
  const auto frames = chunker.push({1, 2, 3, 4, 5});
  ASSERT_EQ(frames.size(), 2u);
  EXPECT_EQ(frames[0], (std::vector<int16_t>{1, 2}));
  EXPECT_EQ(frames[1], (std::vector<int16_t>{3, 4}));
}

TEST(AudioChunkerTest, StereoFrameSizeIsSamplesPerChannelTimesChannels) {
  AudioChunker chunker(/*samplesPerChannel=*/2, /*channels=*/2);
  // 2 samples/channel * 2 channels = 4 interleaved int16 values per frame.
  const auto frames = chunker.push({1, 2, 3, 4, 5, 6});
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0], (std::vector<int16_t>{1, 2, 3, 4}));
}
