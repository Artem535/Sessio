#include "video_frame_source.h"
#include <QCoreApplication>
#include <QSignalSpy>
#include <gtest/gtest.h>
#include <thread>

using pcm::video::VideoFrameSource;

class VideoFrameSourceTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    static int argc = 0;
    static QCoreApplication app(argc, nullptr);
  }
};

TEST_F(VideoFrameSourceTest, WorkerFloodQueuesOneNotificationAndCopiesLatestFrame) {
  VideoFrameSource source;
  QSignalSpy frames(&source, &VideoFrameSource::frameAvailable);
  std::thread producer([&]() {
    QImage frame(3, 2, QImage::Format_ARGB32);
    for (int i = 0; i < 1000; ++i) {
      frame.fill(i);
      source.submitFrame(frame);
    }
    frame.fill(Qt::red);
  });
  producer.join();
  EXPECT_EQ(frames.count(), 0);
  EXPECT_EQ(source.latestFrame().pixel(0, 0), 999u);
  QCoreApplication::processEvents();
  EXPECT_EQ(frames.count(), 1);
  source.submitFrame(QImage(1, 1, QImage::Format_ARGB32));
  QCoreApplication::processEvents();
  EXPECT_EQ(frames.count(), 2);
}

TEST_F(VideoFrameSourceTest, IndependentSourcesAndClearPublishNoStaleFrame) {
  VideoFrameSource a, b;
  QSignalSpy frames(&a, &VideoFrameSource::frameAvailable);
  QImage red(2, 2, QImage::Format_ARGB32);
  red.fill(Qt::red);
  a.submitFrame(red);
  b.submitFrame(red);
  a.clear();
  QCoreApplication::processEvents();
  EXPECT_EQ(frames.count(), 1);
  EXPECT_TRUE(a.latestFrame().isNull());
  EXPECT_EQ(b.latestFrame().pixelColor(0, 0), QColor(Qt::red));
  a.submitFrame(red);
  QCoreApplication::processEvents();
  a.clear();
  QCoreApplication::processEvents();
  EXPECT_EQ(frames.count(), 3);
  EXPECT_TRUE(a.latestFrame().isNull());
}

TEST_F(VideoFrameSourceTest, DeletingSourceDiscardsPendingNotification) {
  auto *source = new VideoFrameSource;
  source->submitFrame(QImage(1, 1, QImage::Format_ARGB32));
  delete source;
  QCoreApplication::processEvents();
}

TEST_F(VideoFrameSourceTest, SubmissionCopiesExternallyOwnedPixelMemory) {
  VideoFrameSource source;
  uchar pixels[] = {10, 20, 30, 255};
  const QImage borrowed(pixels, 1, 1, 4, QImage::Format_RGBA8888);
  source.submitFrame(borrowed);
  pixels[0] = 90;
  EXPECT_EQ(source.latestFrame().pixelColor(0, 0), QColor(10, 20, 30, 255));
  auto snapshot = source.latestFrame();
  snapshot.fill(Qt::black);
  EXPECT_EQ(source.latestFrame().pixelColor(0, 0), QColor(10, 20, 30, 255));
}
