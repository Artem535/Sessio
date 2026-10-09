#pragma once

#include <QObject>
#include <QVideoFrame>
#include <atomic>
#include <livekit/video_source.h>
#include <memory>

namespace pcm::video {

// Runs on the adapter's worker thread: letterboxes a captured screen frame into
// the fixed-size LiveKit source and pushes it, so the GUI thread never blocks.
class ScreenCaptureWorker final : public QObject {
  Q_OBJECT
public:
  explicit ScreenCaptureWorker(std::shared_ptr<livekit::VideoSource> videoSource);

  void setRunning(bool running) { mRunning.store(running); }
  // True while a frame is queued or being converted; the adapter drops new frames meanwhile.
  [[nodiscard]] bool busy() const { return mBusy.load(); }
  void markQueued() { mBusy.store(true); }

public slots:
  void processFrame(const QVideoFrame &frame);

signals:
  void captureFailed(QString reason);

private:
  std::shared_ptr<livekit::VideoSource> mVideoSource;
  std::atomic<bool> mRunning{true};
  std::atomic<bool> mBusy{false};
};

} // namespace pcm::video
