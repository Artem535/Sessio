// spikes/livekit-cpp-spike/test/frame_convert_tests.cpp
#include <gtest/gtest.h>

#include <QImage>

#include "frame_convert.h"

using pcm::spike::videoFrameToLiveKitRGBA;

TEST(FrameConvertTest, ConvertsSolidRedImageToRGBABytes) {
  QImage image(4, 3, QImage::Format_RGB32);
  image.fill(QColor(255, 0, 0));

  const auto frame = videoFrameToLiveKitRGBA(image);

  EXPECT_EQ(frame.width(), 4);
  EXPECT_EQ(frame.height(), 3);
  ASSERT_EQ(frame.dataSize(), static_cast<std::size_t>(4 * 3 * 4));

  const auto *data = frame.data();
  for (int i = 0; i < 4 * 3; ++i) {
    EXPECT_EQ(data[i * 4 + 0], 255) << "pixel " << i << " R";
    EXPECT_EQ(data[i * 4 + 1], 0) << "pixel " << i << " G";
    EXPECT_EQ(data[i * 4 + 2], 0) << "pixel " << i << " B";
    EXPECT_EQ(data[i * 4 + 3], 255) << "pixel " << i << " A";
  }
}

TEST(FrameConvertTest, HandlesArgbSourceFormat) {
  QImage image(2, 2, QImage::Format_ARGB32);
  image.fill(QColor(0, 255, 0, 128));

  const auto frame = videoFrameToLiveKitRGBA(image);
  EXPECT_EQ(frame.width(), 2);
  EXPECT_EQ(frame.height(), 2);
  const auto *data = frame.data();
  EXPECT_EQ(data[0], 0);
  EXPECT_EQ(data[1], 255);
  EXPECT_EQ(data[2], 0);
}
