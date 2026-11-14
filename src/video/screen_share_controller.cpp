#include "screen_share_controller.h"

#include <exception>
#include <utility>

namespace pcm::video {

ScreenShareController::ScreenShareController(Publish publish, Unpublish unpublish, QObject *parent)
    : QObject(parent), mPublish(std::move(publish)), mUnpublish(std::move(unpublish)) {}

ScreenShareController::~ScreenShareController() {
  if (mCapture) mCapture->stop();
}

void ScreenShareController::start(std::unique_ptr<ScreenCapture> capture, const ScreenCaptureTarget &target) {
  stop();
  if (!capture) return;
  ScreenCapture *raw = capture.get();
  connect(raw, &ScreenCapture::started, this, [this, raw] {
    if (mCapture.get() == raw) onStarted();
  });
  connect(raw, &ScreenCapture::captureFailed, this, [this, raw](const QString &reason) {
    if (mCapture.get() == raw) onCaptureFailed(reason);
  });
  mCapture = std::move(capture);
  if (!mCapture->start(target)) {
    releaseCapture();
    emit failed(QStringLiteral("Nothing to share."));
    emit sharingChanged(false);
  }
}

void ScreenShareController::stop() {
  if (!mCapture && !mLive) return;
  const bool wasLive = std::exchange(mLive, false);
  releaseCapture();
  if (wasLive) mUnpublish();
  emit sharingChanged(false);
}

void ScreenShareController::discard() {
  mLive = false;
  releaseCapture();
}

void ScreenShareController::onStarted() {
  if (mLive) return;
  try {
    mPublish(*mCapture);
  } catch (const std::exception &e) {
    releaseCapture();
    emit failed(QStringLiteral("Failed to share the screen: %1").arg(QString::fromUtf8(e.what())));
    emit sharingChanged(false);
    return;
  }
  mLive = true;
  emit sharingChanged(true);
}

void ScreenShareController::onCaptureFailed(const QString &reason) {
  const bool wasLive = std::exchange(mLive, false);
  releaseCapture();
  if (wasLive) mUnpublish();
  emit failed(wasLive ? QStringLiteral("Screen sharing stopped: %1").arg(reason)
                      : QStringLiteral("Screen sharing could not start: %1").arg(reason));
  emit sharingChanged(false);
}

void ScreenShareController::releaseCapture() {
  if (!mCapture) return;
  ScreenCapture *capture = mCapture.release();
  capture->disconnect(this);
  capture->stop();
  // A failure or start signal of this capture may be what got us here.
  capture->deleteLater();
}

} // namespace pcm::video
