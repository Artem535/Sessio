#include "frame_convert.h"

#include <gtest/gtest.h>

TEST(FrameConvertTest, SolidRedRgb32ConvertsToExpectedRgbaBytes) {
  QImage image(4, 2, QImage::Format_RGB32);
  image.fill(QColor(255, 0, 0));

  const auto frame = pcm::video::videoFrameToLiveKitRGBA(image);

  ASSERT_EQ(frame.width(), 4u);
  ASSERT_EQ(frame.height(), 2u);
  const auto *data = frame.data();
  for (int i = 0; i < 4 * 2; ++i) {
    EXPECT_EQ(data[i * 4 + 0], 255) << "pixel " << i << " red";
    EXPECT_EQ(data[i * 4 + 1], 0) << "pixel " << i << " green";
    EXPECT_EQ(data[i * 4 + 2], 0) << "pixel " << i << " blue";
    EXPECT_EQ(data[i * 4 + 3], 255) << "pixel " << i << " alpha";
  }
}

TEST(FrameConvertTest, Argb32SourceReordersChannelsCorrectly) {
  QImage image(2, 2, QImage::Format_ARGB32);
  image.fill(QColor(10, 20, 30, 200));

  const auto frame = pcm::video::videoFrameToLiveKitRGBA(image);

  const auto *data = frame.data();
  EXPECT_EQ(data[0], 10);
  EXPECT_EQ(data[1], 20);
  EXPECT_EQ(data[2], 30);
  EXPECT_EQ(data[3], 200);
}
