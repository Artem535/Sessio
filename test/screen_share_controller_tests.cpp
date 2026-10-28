#include "fake_screen_capture.h"
#include "screen_share_controller.h"

#include <QGuiApplication>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <gtest/gtest.h>
#include <stdexcept>

using pcm::video::ScreenCapture;
using pcm::video::ScreenShareController;

namespace {

struct Harness {
  int publishes{0};
  int unpublishes{0};
  bool publishThrows{false};
  ScreenShareController controller{[this](ScreenCapture &) {
                                      if (publishThrows) throw std::runtime_error("room refused");
                                      ++publishes;
                                    },
                                    [this] { ++unpublishes; }};
  QSignalSpy sharing{&controller, &ScreenShareController::sharingChanged};
  QSignalSpy failures{&controller, &ScreenShareController::failed};

  // Starts a share with a fresh fake capture and returns it (owned by the controller).
  QPointer<FakeScreenCapture> share(const std::shared_ptr<FakeScreenCapture::Log> &log, bool startResult = true) {
    auto capture = std::make_unique<FakeScreenCapture>(log, startResult);
    QPointer<FakeScreenCapture> raw = capture.get();
    controller.start(std::move(capture), {});
    return raw;
  }
};

void settle() { QTest::qWait(20); }

} // namespace

// The 2026-10-28 call: the Wayland portal refused every share, yet the screen
// track was published right away and the other side watched a black screen.
TEST(ScreenShareControllerTest, CaptureThatFailsBeforeItsFirstFrameIsNeverPublished) {
  Harness h;
  auto log = std::make_shared<FakeScreenCapture::Log>();
  QPointer<FakeScreenCapture> capture = h.share(log);
  EXPECT_TRUE(h.controller.isPending());
  EXPECT_EQ(h.publishes, 0) << "nothing may be published before the capture delivers a frame";

  capture->fail(QStringLiteral("Screen selection was cancelled."));
  settle();

  EXPECT_EQ(h.publishes, 0);
  EXPECT_EQ(h.unpublishes, 0);
  EXPECT_FALSE(h.controller.isSharing());
  EXPECT_FALSE(h.controller.isPending());
  EXPECT_EQ(log->stops, 1) << "the portal session must be closed";
  EXPECT_TRUE(capture.isNull()) << "the failed capture is freed";
  ASSERT_EQ(h.failures.count(), 1);
  EXPECT_TRUE(h.failures.at(0).at(0).toString().contains(QStringLiteral("cancelled")));
  ASSERT_FALSE(h.sharing.isEmpty());
  EXPECT_FALSE(h.sharing.last().at(0).toBool()) << "the UI is told to go back to idle";
}

TEST(ScreenShareControllerTest, TrackIsPublishedOnlyOnceTheFirstFrameArrives) {
  Harness h;
  auto log = std::make_shared<FakeScreenCapture::Log>();
  QPointer<FakeScreenCapture> capture = h.share(log);
  settle();
  EXPECT_EQ(h.publishes, 0);
  EXPECT_FALSE(h.controller.isSharing());

  capture->deliverFirstFrame();
  capture->deliverFirstFrame();
  settle();

  EXPECT_EQ(h.publishes, 1);
  EXPECT_TRUE(h.controller.isSharing());
  ASSERT_EQ(h.sharing.count(), 1);
  EXPECT_TRUE(h.sharing.at(0).at(0).toBool());
}

TEST(ScreenShareControllerTest, CaptureEndingWhileLiveUnpublishesAndReports) {
  Harness h;
  auto log = std::make_shared<FakeScreenCapture::Log>();
  QPointer<FakeScreenCapture> capture = h.share(log);
  capture->deliverFirstFrame();
  settle();
  ASSERT_TRUE(h.controller.isSharing());

  capture->fail(QStringLiteral("The desktop ended screen sharing."));
  settle();

  EXPECT_EQ(h.unpublishes, 1);
  EXPECT_FALSE(h.controller.isSharing());
  EXPECT_EQ(log->stops, 1);
  ASSERT_EQ(h.failures.count(), 1);
  EXPECT_FALSE(h.sharing.last().at(0).toBool());
}

TEST(ScreenShareControllerTest, ShareStopShareAgainCleansUpTheFirstCapture) {
  Harness h;
  auto firstLog = std::make_shared<FakeScreenCapture::Log>();
  QPointer<FakeScreenCapture> first = h.share(firstLog);
  first->deliverFirstFrame();
  settle();
  h.controller.stop();
  EXPECT_EQ(h.unpublishes, 1);
  EXPECT_EQ(firstLog->stops, 1);
  EXPECT_FALSE(h.controller.isSharing());

  auto secondLog = std::make_shared<FakeScreenCapture::Log>();
  QPointer<FakeScreenCapture> second = h.share(secondLog);
  second->deliverFirstFrame();
  settle();

  EXPECT_EQ(h.publishes, 2);
  EXPECT_TRUE(h.controller.isSharing());
  EXPECT_TRUE(first.isNull());
  EXPECT_EQ(secondLog->stops, 0);
  EXPECT_EQ(h.failures.count(), 0);
}

TEST(ScreenShareControllerTest, ReplacingAPendingShareIgnoresTheOldCapturesSignals) {
  Harness h;
  auto firstLog = std::make_shared<FakeScreenCapture::Log>();
  QPointer<FakeScreenCapture> first = h.share(firstLog);
  first->fail(QStringLiteral("late failure"));
  auto secondLog = std::make_shared<FakeScreenCapture::Log>();
  QPointer<FakeScreenCapture> second = h.share(secondLog);
  second->deliverFirstFrame();
  settle();

  EXPECT_EQ(firstLog->stops, 1);
  EXPECT_EQ(h.failures.count(), 0);
  EXPECT_TRUE(h.controller.isSharing());
}

TEST(ScreenShareControllerTest, RefusedPublishStopsTheCaptureAndReports) {
  Harness h;
  h.publishThrows = true;
  auto log = std::make_shared<FakeScreenCapture::Log>();
  QPointer<FakeScreenCapture> capture = h.share(log);
  capture->deliverFirstFrame();
  settle();

  EXPECT_FALSE(h.controller.isSharing());
  EXPECT_FALSE(h.controller.isPending());
  EXPECT_EQ(log->stops, 1);
  ASSERT_EQ(h.failures.count(), 1);
  EXPECT_TRUE(h.failures.at(0).at(0).toString().contains(QStringLiteral("room refused")));
  EXPECT_FALSE(h.sharing.last().at(0).toBool());
}

TEST(ScreenShareControllerTest, UnusableTargetReportsNothingToShare) {
  Harness h;
  auto log = std::make_shared<FakeScreenCapture::Log>();
  h.share(log, false);
  EXPECT_FALSE(h.controller.isPending());
  EXPECT_EQ(h.failures.count(), 1);
  EXPECT_EQ(h.publishes, 0);
}

TEST(ScreenShareControllerTest, DiscardOnTeardownStopsCaptureWithoutUnpublishingOrSignals) {
  Harness h;
  auto log = std::make_shared<FakeScreenCapture::Log>();
  QPointer<FakeScreenCapture> capture = h.share(log);
  capture->deliverFirstFrame();
  settle();
  h.sharing.clear();

  h.controller.discard();
  settle();

  EXPECT_EQ(log->stops, 1);
  EXPECT_EQ(h.unpublishes, 0) << "teardown unpublishes every track itself";
  EXPECT_TRUE(h.sharing.isEmpty());
  EXPECT_FALSE(h.controller.isSharing());
}

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
