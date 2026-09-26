#include "video_capture_worker.h"

#include "frame_convert.h"

namespace pcm::video {

VideoCaptureWorker::VideoCaptureWorker(std::shared_ptr<livekit::VideoSource> videoSource,
                                       std::atomic<int> &framesCaptured)
    : mVideoSource(std::move(videoSource)), mFramesCaptured(framesCaptured) {}

void VideoCaptureWorker::processFrame(const QVideoFrame &frame) {
  if (!mRunning.load() || !frame.isValid()) {
    return;
  }

  const QImage image = frame.toImage();
  if (image.isNull()) {
    return;
  }

  try {
    auto liveKitFrame = videoFrameToLiveKitRGBA(image);
    mVideoSource->captureFrame(liveKitFrame, 0, livekit::VideoRotation::VIDEO_ROTATION_0);
    mFramesCaptured.fetch_add(1);
    emit frameCaptured();
  } catch (const std::exception &e) {
    emit captureFailed(QString::fromUtf8(e.what()));
  }
}

} // namespace pcm::video
