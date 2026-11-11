#pragma once

#include "screen_capture.h"

#include <QMetaObject>
#include <memory>

// A capture whose outcome the test decides: the first frame (started) or a
// failure (a cancelled portal picker, a portal error). Like the real adapter,
// both signals are delivered from the event loop, never from start().
class FakeScreenCapture final : public pcm::video::ScreenCapture {
  Q_OBJECT
public:
  struct Log {
    int starts{0};
    int stops{0};
  };

  explicit FakeScreenCapture(std::shared_ptr<Log> log, bool startResult = true)
      : mLog(std::move(log)), mStartResult(startResult) {}

  [[nodiscard]] std::shared_ptr<livekit::VideoSource> videoSource() const override { return nullptr; }
  bool start(const pcm::video::ScreenCaptureTarget &) override {
    ++mLog->starts;
    return mStartResult;
  }
  void stop() override { ++mLog->stops; }

  void deliverFirstFrame() {
    QMetaObject::invokeMethod(this, [this] { emit started(); }, Qt::QueuedConnection);
  }
  void fail(const QString &reason) {
    QMetaObject::invokeMethod(this, [this, reason] { emit captureFailed(reason); }, Qt::QueuedConnection);
  }

private:
  std::shared_ptr<Log> mLog;
  bool mStartResult;
};
