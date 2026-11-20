#include "video_frame_source.h"
#include <QMetaObject>
#include <QMutexLocker>

namespace pcm::video {

VideoFrameSource::VideoFrameSource(QObject *parent) : QObject(parent) {}

void VideoFrameSource::submitFrame(const QImage &frame) {
  replaceFrame(frame.copy());
}

void VideoFrameSource::clear() {
  replaceFrame({});
}

QImage VideoFrameSource::latestFrame() const {
  QMutexLocker lock(&mMutex);
  return mLatestFrame;
}

void VideoFrameSource::replaceFrame(QImage frame) {
  QMutexLocker lock(&mMutex);
  mLatestFrame = std::move(frame);
  if (mNotificationPending)
    return;
  mNotificationPending = true;
  QMetaObject::invokeMethod(this, [this]() {
    {
      QMutexLocker lock(&mMutex);
      mNotificationPending = false;
    }
    emit frameAvailable();
  }, Qt::QueuedConnection);
}

} // namespace pcm::video
