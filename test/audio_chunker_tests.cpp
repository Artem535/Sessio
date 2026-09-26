#include "audio_chunker.h"

#include <gtest/gtest.h>

TEST(AudioChunkerTest, BelowThresholdProducesNoFrames) {
  pcm::video::AudioChunker chunker(480, 1);
  const auto frames = chunker.push(std::vector<int16_t>(100, 0));
  EXPECT_TRUE(frames.empty());
}

TEST(AudioChunkerTest, ExactlyOneFrameWorthProducesOneFrame) {
  pcm::video::AudioChunker chunker(480, 1);
  const auto frames = chunker.push(std::vector<int16_t>(480, 7));
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0].size(), 480u);
  EXPECT_EQ(frames[0][0], 7);
}

TEST(AudioChunkerTest, RemainderCarriesAcrossCalls) {
  pcm::video::AudioChunker chunker(480, 1);
  EXPECT_TRUE(chunker.push(std::vector<int16_t>(300, 1)).empty());
  const auto frames = chunker.push(std::vector<int16_t>(300, 2));
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0].size(), 480u);
  EXPECT_EQ(frames[0][0], 1);
  EXPECT_EQ(frames[0][299], 1);
  EXPECT_EQ(frames[0][300], 2);
}

TEST(AudioChunkerTest, MultipleFramesFromOnePush) {
  pcm::video::AudioChunker chunker(100, 1);
  const auto frames = chunker.push(std::vector<int16_t>(250, 9));
  ASSERT_EQ(frames.size(), 2u);
  EXPECT_EQ(frames[0].size(), 100u);
  EXPECT_EQ(frames[1].size(), 100u);
}

TEST(AudioChunkerTest, StereoFrameSizingUsesSamplesPerChannelTimesChannels) {
  pcm::video::AudioChunker chunker(100, 2);
  const auto frames = chunker.push(std::vector<int16_t>(200, 3));
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0].size(), 200u);
}

TEST(AudioChunkerTest, ResetDiscardsPartialRemainder) {
  pcm::video::AudioChunker chunker(480, 1);
  EXPECT_TRUE(chunker.push(std::vector<int16_t>(300, 1)).empty());
  chunker.reset();
  EXPECT_TRUE(chunker.push(std::vector<int16_t>(300, 2)).empty());
  const auto frames = chunker.push(std::vector<int16_t>(180, 2));
  ASSERT_EQ(frames.size(), 1u);
  for (const auto sample : frames[0]) {
    EXPECT_EQ(sample, 2);
  }
}
