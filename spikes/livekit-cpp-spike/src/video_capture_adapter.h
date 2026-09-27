#pragma once

#include <QCamera>
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QObject>
#include <QThread>
#include <QVideoSink>

#include <atomic>
#include <memory>

#include "livekit/livekit.h"

class VideoCaptureWorker;

// Drives a QCamera through Qt Multimedia and pushes every captured frame,
// converted to RGBA, into a livekit::VideoSource. Owns no LiveKit track —
// callers publish videoSource() themselves (see spike_window.cpp).
//
// onVideoFrameChanged() runs on the GUI thread — that's where QVideoSink
// delivers videoFrameChanged, ~30 times/sec for a typical camera — but only
// forwards the frame. The CPU-heavy work (QImage conversion, RGBA repack,
// the captureFrame() FFI call) happens on a dedicated worker thread
// (VideoCaptureWorker), so it never blocks the GUI event loop that local
// preview repaint / remote paintGL() / the rest of the UI also depend on.
class VideoCaptureAdapter final : public QObject {
  Q_OBJECT

public:
  explicit VideoCaptureAdapter(QObject *parent = nullptr);
  ~VideoCaptureAdapter() override;

  [[nodiscard]] std::shared_ptr<livekit::VideoSource> videoSource() const { return mVideoSource; }
  [[nodiscard]] QVideoSink *previewSink() { return &mSink; }
  [[nodiscard]] int framesCaptured() const { return mFramesCaptured.load(); }

  // Starts (or restarts, if already running) capture on the given device.
  void start(const QCameraDevice &device);
  void stop();

signals:
  void frameCaptured();

private slots:
  void onVideoFrameChanged(const QVideoFrame &frame);

private:
  std::shared_ptr<livekit::VideoSource> mVideoSource;
  std::unique_ptr<QCamera> mCamera;
  QMediaCaptureSession mSession;
  QVideoSink mSink;
  std::atomic<int> mFramesCaptured{0};

  // Worker thread + worker object, created fresh in start() and torn down
  // in stop() (same start()/stop() cycle the camera itself goes through, so
  // switching devices mid-session gets a clean worker each time too). Never
  // left running after stop() returns.
  QThread mWorkerThread;
  std::unique_ptr<VideoCaptureWorker> mWorker;
};
