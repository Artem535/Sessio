#include "screen_capture_adapter.h"

#include <QGuiApplication>
#include <QPointer>
#include <QScreenCapture>
#include <QTest>
#include <gtest/gtest.h>
#include <livekit/livekit.h>

using pcm::video::ScreenCaptureAdapter;
using pcm::video::ScreenCaptureTarget;

namespace {

ScreenCaptureTarget primaryScreen() {
  ScreenCaptureTarget target;
  target.screen = QGuiApplication::primaryScreen();
  return target;
}

// Qt reports portal failures (e.g. from a D-Bus reply) by emitting
// errorOccurred from inside QScreenCapture's own code.
void reportPortalFailure(ScreenCaptureAdapter &adapter) {
  auto *capture = adapter.findChild<QScreenCapture *>();
  ASSERT_NE(capture, nullptr);
  emit capture->errorOccurred(QScreenCapture::InternalError, QStringLiteral("portal"));
}

} // namespace

TEST(ScreenCaptureAdapterTest, FailureIsDeliveredAfterQtLeavesTheCaptureSoItCanBeDestroyed) {
  // LiveKitVideoProvider destroys the adapter (and its QScreenCapture) when a
  // capture fails. Doing that inside errorOccurred freed the capture while Qt
  // was still running its code: a crash in libQt6Core during screen sharing.
  QPointer<ScreenCaptureAdapter> adapter = new ScreenCaptureAdapter;
  ASSERT_TRUE(adapter->start(primaryScreen()));
  int failures = 0;
  QObject::connect(adapter, &ScreenCaptureAdapter::captureFailed, adapter, [&](const QString &reason) {
    if (reason != "portal") return;
    ++failures;
    delete adapter.data();
  });
  reportPortalFailure(*adapter);
  EXPECT_EQ(failures, 0) << "failure must not be handled inside Qt's signal emission";
  for (int i = 0; i < 50 && failures == 0; ++i) QTest::qWait(10);
  EXPECT_EQ(failures, 1);
  EXPECT_TRUE(adapter.isNull());
}

TEST(ScreenCaptureAdapterTest, FailureOfAReplacedCaptureDoesNotStopTheNewShare) {
  ScreenCaptureAdapter adapter;
  ASSERT_TRUE(adapter.start(primaryScreen()));
  int failures = 0;
  QObject::connect(&adapter, &ScreenCaptureAdapter::captureFailed, &adapter,
                   [&](const QString &reason) { if (reason == "portal") ++failures; });
  reportPortalFailure(adapter);
  ASSERT_TRUE(adapter.start(primaryScreen())); // the user picked another screen
  QTest::qWait(50);
  EXPECT_EQ(failures, 0);
  EXPECT_TRUE(adapter.active());
}

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  // ScreenCaptureAdapter's livekit::VideoSource makes an FFI call on construction.
  livekit::initialize(livekit::LogLevel::Warn);
  ::testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  livekit::shutdown();
  return result;
}
