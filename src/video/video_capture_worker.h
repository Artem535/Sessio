#pragma once

#include <QObject>
#include <QVideoFrame>
#include <atomic>
#include <livekit/video_source.h>
#include <memory>

namespace pcm::video {

// Runs on a dedicated QThread (owned by VideoCaptureAdapter). Does the
// CPU-heavy work of converting a captured QVideoFrame and calling into the
// LiveKit FFI, off the GUI thread, so GUI repaint is never blocked.
class VideoCaptureWorker final : public QObject {
  Q_OBJECT
public:
  VideoCaptureWorker(std::shared_ptr<livekit::VideoSource> videoSource,
                     std::atomic<int> &framesCaptured);

  void setRunning(bool running) { mRunning.store(running); }

public slots:
  void processFrame(const QVideoFrame &frame);

signals:
  void frameCaptured();
  void captureFailed(QString reason);

private:
  std::shared_ptr<livekit::VideoSource> mVideoSource;
  std::atomic<int> &mFramesCaptured;
  std::atomic<bool> mRunning{true};
};

} // namespace pcm::video
