#pragma once

#include "screen_capture_target.h"
#include <QElapsedTimer>
#include <QMediaCaptureSession>
#include <QObject>
#include <QScreen>
#include <QScreenCapture>
#include <QThread>
#include <QVideoSink>
#include <QWindowCapture>
#include <livekit/video_source.h>
#include <memory>

namespace pcm::video {

class ScreenCaptureWorker;

// Captures a screen or window through Qt Multimedia and feeds a livekit::VideoSource.
// Frames are throttled: slides and documents do not need camera frame rates.
class ScreenCaptureAdapter final : public QObject {
  Q_OBJECT
public:
  static constexpr int kWidth = 1920;
  static constexpr int kHeight = 1080;
  static constexpr int kMinFrameIntervalMs = 100;

  explicit ScreenCaptureAdapter(QObject *parent = nullptr);
  ~ScreenCaptureAdapter() override;

  [[nodiscard]] std::shared_ptr<livekit::VideoSource> videoSource() const { return mVideoSource; }
  [[nodiscard]] bool active() const { return mActive; }

  // Starts (or retargets) capture. Returns false when the target is invalid.
  bool start(const ScreenCaptureTarget &target);
  void stop();

signals:
  // The OS or the user ended the capture (window closed, permission revoked).
  void captureFailed(QString reason);

private:
  void onVideoFrameChanged(const QVideoFrame &frame);

  std::shared_ptr<livekit::VideoSource> mVideoSource;
  std::unique_ptr<QScreenCapture> mScreenCapture;
  std::unique_ptr<QWindowCapture> mWindowCapture;
  QMediaCaptureSession mSession;
  QVideoSink mSink;
  QThread mWorkerThread;
  std::unique_ptr<ScreenCaptureWorker> mWorker;
  QElapsedTimer mSinceLastFrame;
  bool mActive{false};
};

} // namespace pcm::video
