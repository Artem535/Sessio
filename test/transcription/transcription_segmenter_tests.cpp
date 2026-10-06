#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <memory>

#include "phrase_segmenter.h"
#include "transcription_test_support.h"

using namespace pcm::transcription;

namespace {

std::vector<float> block(float value, size_t n) { return std::vector<float>(n, value); }

PhraseSegmenter makeSegmenter() {
  return PhraseSegmenter(std::make_unique<pcm::transcription::testing::FakeVad>());
}

}  // namespace

TEST(PhraseSegmenterTest, EmitsClosedPhraseWithStartSample) {
  auto seg = makeSegmenter();
  std::vector<float> audio = block(0.0f, 1024);
  const auto speech = block(0.5f, 1024);
  audio.insert(audio.end(), speech.begin(), speech.end());
  const auto quiet = block(0.0f, 512);
  audio.insert(audio.end(), quiet.begin(), quiet.end());

  const auto phrases = seg.feed(audio);
  ASSERT_EQ(phrases.size(), 1u);
  EXPECT_EQ(phrases[0].start_sample, 1024);
  EXPECT_EQ(phrases[0].samples.size(), 1024u);
}

TEST(PhraseSegmenterTest, BuffersPartialWindowsAcrossCalls) {
  auto seg = makeSegmenter();
  std::vector<float> all = block(0.5f, 1024);
  const auto quiet = block(0.0f, 512);
  all.insert(all.end(), quiet.begin(), quiet.end());

  std::vector<SpeechSegment> got;
  for (size_t i = 0; i < all.size(); i += 100) {
    const size_t n = std::min<size_t>(100, all.size() - i);
    auto part = seg.feed(std::span<const float>(all.data() + i, n));
    got.insert(got.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
  }
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0].start_sample, 0);
}

TEST(PhraseSegmenterTest, FlushClosesOpenPhraseAndPadsPartialWindow) {
  auto seg = makeSegmenter();
  EXPECT_TRUE(seg.feed(block(0.5f, 700)).empty());  // one full window + 188 pending
  const auto phrases = seg.flush();
  ASSERT_EQ(phrases.size(), 1u);
  EXPECT_EQ(phrases[0].samples.size(), 1024u);  // second window zero-padded
}

TEST(PhraseSegmenterTest, FlushWithNothingOpenReturnsNothing) {
  auto seg = makeSegmenter();
  EXPECT_TRUE(seg.feed(block(0.0f, 2048)).empty());
  EXPECT_TRUE(seg.flush().empty());
}
