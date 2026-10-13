#include "screen_capture_worker.h"

#include "frame_convert.h"

#include <QImage>
#include <QPainter>

namespace pcm::video {

ScreenCaptureWorker::ScreenCaptureWorker(std::shared_ptr<livekit::VideoSource> videoSource)
    : mVideoSource(std::move(videoSource)) {}

void ScreenCaptureWorker::processFrame(const QVideoFrame &frame) {
  struct Done {
    std::atomic<bool> &flag;
    ~Done() { flag.store(false); }
  } done{mBusy};
  if (!mRunning.load() || !frame.isValid()) return;
  const QImage image = frame.toImage();
  if (image.isNull()) return;

  // The source has a fixed size; keep the screen's aspect ratio on a black canvas.
  QImage canvas(mVideoSource->width(), mVideoSource->height(), QImage::Format_RGBA8888);
  canvas.fill(Qt::black);
  const QImage scaled = image.scaled(canvas.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
  QPainter painter(&canvas);
  painter.drawImage((canvas.width() - scaled.width()) / 2, (canvas.height() - scaled.height()) / 2, scaled);
  painter.end();
  try {
    mVideoSource->captureFrame(videoFrameToLiveKitRGBA(canvas), 0, livekit::VideoRotation::VIDEO_ROTATION_0);
  } catch (const std::exception &e) {
    emit captureFailed(QString::fromUtf8(e.what()));
  }
}

} // namespace pcm::video
