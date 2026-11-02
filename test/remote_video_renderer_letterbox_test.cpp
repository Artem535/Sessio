#include "remote_video_renderer.h"

#include <gtest/gtest.h>

using pcm::video::RemoteVideoRenderer;

// scaledFrameRect() is the pure geometry calculation paintGL() uses to
// place a KeepAspectRatio-scaled frame inside the widget. No QOpenGLWidget/
// GL context is exercised here — this only pins down the math that
// determines the letterbox region paintGL() must now fill.
TEST(RemoteVideoRendererLetterboxTest, WidthConstrainedFrameIsCenteredWithVerticalBars) {
  const QRect placed = RemoteVideoRenderer::scaledFrameRect(QSize(1280, 720), QSize(1280, 1000));
  // Widget is taller than the frame's aspect ratio requires: full width, bars top/bottom.
  EXPECT_EQ(placed.width(), 1280);
  EXPECT_LT(placed.height(), 1000);
  EXPECT_GT(placed.top(), 0);
}

TEST(RemoteVideoRendererLetterboxTest, HeightConstrainedFrameIsCenteredWithHorizontalBars) {
  const QRect placed = RemoteVideoRenderer::scaledFrameRect(QSize(1280, 720), QSize(2000, 720));
  EXPECT_EQ(placed.height(), 720);
  EXPECT_LT(placed.width(), 2000);
  EXPECT_GT(placed.left(), 0);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
