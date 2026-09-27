#pragma once

#include <QCamera>
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QObject>
#include <QThread>
#include <QVideoSink>
#include <atomic>
#include <livekit/video_source.h>
#include <memory>

namespace pcm::video {

class VideoCaptureWorker;

// Captures camera frames via Qt Multimedia and feeds them into a
// livekit::VideoSource. The conversion + LiveKit FFI call happens on a
// dedicated worker thread (VideoCaptureWorker) so the GUI thread is never
// blocked by capture.
class VideoCaptureAdapter final : public QObject {
  Q_OBJECT
public:
  explicit VideoCaptureAdapter(QObject *parent = nullptr);
  ~VideoCaptureAdapter() override;

  [[nodiscard]] std::shared_ptr<livekit::VideoSource> videoSource() const { return mVideoSource; }
  [[nodiscard]] QVideoSink *previewSink() { return &mSink; }
  [[nodiscard]] int framesCaptured() const { return mFramesCaptured.load(); }

  // Starts (or restarts, for a device switch) capture from the given device.
  void start(const QCameraDevice &device);
  void stop();

signals:
  void frameCaptured();
  void captureFailed(QString reason);

private slots:
  void onVideoFrameChanged(const QVideoFrame &frame);

private:
  static constexpr int kVideoWidth = 1280;
  static constexpr int kVideoHeight = 720;

  std::shared_ptr<livekit::VideoSource> mVideoSource;
  std::unique_ptr<QCamera> mCamera;
  QMediaCaptureSession mSession;
  QVideoSink mSink;
  std::atomic<int> mFramesCaptured{0};
  QThread mWorkerThread;
  std::unique_ptr<VideoCaptureWorker> mWorker;
};

} // namespace pcm::video
