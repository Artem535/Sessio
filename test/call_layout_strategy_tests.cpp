#include "call_layout_strategy.h"
#include <gtest/gtest.h>

using pcm::video::CallLayoutStrategy;

TEST(CallLayoutStrategyTest, OneRemoteAndLocalUsePictureInPicture) {
  const auto result = CallLayoutStrategy::select(2, true, QSize(1280, 720));
  EXPECT_TRUE(result.pictureInPicture);
  EXPECT_EQ(result.rows, 1);
  EXPECT_EQ(result.columns, 1);
}

TEST(CallLayoutStrategyTest, ThreeParticipantsUseEqualLandscapeTiles) {
  const auto result = CallLayoutStrategy::select(3, true, QSize(1280, 720));
  EXPECT_FALSE(result.pictureInPicture);
  EXPECT_EQ(result.rows, 2);
  EXPECT_EQ(result.columns, 2);
}

TEST(CallLayoutStrategyTest, PortraitChoosesMoreRowsThanLandscape) {
  const auto landscape = CallLayoutStrategy::select(6, true, QSize(1280, 720));
  const auto portrait = CallLayoutStrategy::select(6, true, QSize(480, 900));
  EXPECT_GT(portrait.rows, landscape.rows);
  EXPECT_LT(portrait.columns, landscape.columns);
}

TEST(CallLayoutStrategyTest, SixAndTenAllFitWithPositiveAspectPreservingSize) {
  for (const auto count : {6, 10}) {
    for (const auto stage : {QSize(1280, 720), QSize(480, 900)}) {
      const auto result = CallLayoutStrategy::select(count, true, stage);
      EXPECT_GE(result.rows * result.columns, count);
      EXPECT_GT(result.tileSize.height(), 0);
      EXPECT_NEAR(double(result.tileSize.width()) / result.tileSize.height(), 16.0 / 9.0, .04);
      EXPECT_LE(result.tileSize.width() * result.columns + 8 * (result.columns - 1), stage.width());
      EXPECT_LE(result.tileSize.height() * result.rows + 8 * (result.rows - 1), stage.height());
    }
  }
}
