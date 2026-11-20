#pragma once

#include "screen_capture.h"
#include <QElapsedTimer>
#include <QImage>
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

// Captures a screen or window and feeds a livekit::VideoSource. On Wayland it
// goes through the ScreenCast portal and PipeWire itself; elsewhere through
// Qt Multimedia. Frames are throttled: slides and documents do not need camera frame rates.
class ScreenCaptureAdapter final : public ScreenCapture {
  Q_OBJECT
public:
  static constexpr int kWidth = 1920;
  static constexpr int kHeight = 1080;
  static constexpr int kMinFrameIntervalMs = 100;

  explicit ScreenCaptureAdapter(QObject *parent = nullptr);
  ~ScreenCaptureAdapter() override;

  [[nodiscard]] std::shared_ptr<livekit::VideoSource> videoSource() const override { return mVideoSource; }
  [[nodiscard]] bool active() const { return mActive; }

  // Starts (or retargets) capture. Returns false when the target is invalid.
  bool start(const ScreenCaptureTarget &target) override;
  void stop() override;

private:
  void startQtCapture(const ScreenCaptureTarget &target);
  void startPortalCapture(const ScreenCaptureTarget &target);
  void onVideoFrameChanged(const QVideoFrame &frame);
  void onFrameDelivered(quint64 generation);
  void reportFailure(const QString &message);

  std::shared_ptr<livekit::VideoSource> mVideoSource;
  std::unique_ptr<QScreenCapture> mScreenCapture;
  std::unique_ptr<QWindowCapture> mWindowCapture;
  // The Wayland portal session and its PipeWire stream (Linux builds only).
  struct PortalCapture;
  std::unique_ptr<PortalCapture> mPortal;
  QMediaCaptureSession mSession;
  QVideoSink mSink;
  QThread mWorkerThread;
  std::unique_ptr<ScreenCaptureWorker> mWorker;
  QElapsedTimer mSinceLastFrame;
  bool mActive{false};
  bool mStarted{false};
  quint64 mGeneration{0}; // bumped by stop(): signals of a replaced capture are dropped
};

} // namespace pcm::video
