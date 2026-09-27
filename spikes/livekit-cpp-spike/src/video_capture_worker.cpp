// Throwaway spike for issue #77 — not production quality.
#include "video_capture_worker.h"

#include <QImage>

#include <iostream>
#include <utility>

#include "frame_convert.h"

VideoCaptureWorker::VideoCaptureWorker(std::shared_ptr<livekit::VideoSource> videoSource,
                                        std::atomic<int> &framesCaptured)
    : mVideoSource(std::move(videoSource)), mFramesCaptured(framesCaptured) {}

void VideoCaptureWorker::processFrame(const QVideoFrame &frame) {
  if (!mRunning.load()) {
    return;  // VideoCaptureAdapter::stop() ran while this queued call was pending
  }
  if (!frame.isValid()) {
    return;
  }
  const QImage image = frame.toImage();
  if (image.isNull()) {
    return;
  }

  // Exact same conversion + captureFrame() call (same args, same try/catch,
  // same stderr-only error reporting) that used to run inline in
  // VideoCaptureAdapter::onVideoFrameChanged on the GUI thread.
  const auto liveKitFrame = pcm::spike::videoFrameToLiveKitRGBA(image);
  try {
    mVideoSource->captureFrame(liveKitFrame, 0, livekit::VideoRotation::VIDEO_ROTATION_0);
    mFramesCaptured.fetch_add(1);
    emit frameCaptured();
  } catch (const std::exception &e) {
    std::cerr << "[video capture] captureFrame failed: " << e.what() << std::endl;
  }
}
