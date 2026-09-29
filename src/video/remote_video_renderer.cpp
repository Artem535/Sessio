#include "remote_video_renderer.h"

#include <QMutexLocker>
#include <QPainter>

namespace pcm::video {

RemoteVideoRenderer::RemoteVideoRenderer(QWidget *parent) : QOpenGLWidget(parent) {}

RemoteVideoRenderer::~RemoteVideoRenderer() {
  detach();
}

void RemoteVideoRenderer::attachTrack(const std::shared_ptr<livekit::Track> &track) {
  detach();

  if (!track) {
    return;
  }

  livekit::VideoStream::Options options;
  options.format = livekit::VideoBufferType::RGBA;
  mStream = livekit::VideoStream::fromTrack(track, options);
  if (!mStream) {
    return;
  }

  mRunning.store(true);
  mReaderThread = std::thread(&RemoteVideoRenderer::readerLoop, this);
}

void RemoteVideoRenderer::detach() {
  mRunning.store(false);
  // Wakes a thread currently blocked in mStream->read() — without this,
  // detach() (or the destructor) can hang forever if no more frames arrive.
  if (mStream) {
    mStream->close();
  }
  if (mReaderThread.joinable()) {
    mReaderThread.join();
  }
  mStream.reset();
}

void RemoteVideoRenderer::readerLoop() {
  livekit::VideoFrameEvent event;
  while (mRunning.load()) {
    if (!mStream->read(event)) {
      break;
    }
    const auto &frame = event.frame;
    QImage image(frame.data(), static_cast<int>(frame.width()), static_cast<int>(frame.height()),
                QImage::Format_RGBA8888);
    setLatestFrame(image.copy());
  }
}

void RemoteVideoRenderer::setLatestFrame(const QImage &image) {
  {
    QMutexLocker locker(&mFrameMutex);
    mLatestFrame = image;
  }
  QMetaObject::invokeMethod(this, QOverload<>::of(&QOpenGLWidget::update),
                            Qt::QueuedConnection);
}

QRect RemoteVideoRenderer::scaledFrameRect(const QSize &frameSize, const QSize &widgetSize) {
  const QSize scaled = frameSize.scaled(widgetSize, Qt::KeepAspectRatio);
  const QPoint topLeft((widgetSize.width() - scaled.width()) / 2,
                        (widgetSize.height() - scaled.height()) / 2);
  return QRect(topLeft, scaled);
}

void RemoteVideoRenderer::paintGL() {
  QImage frame;
  {
    QMutexLocker locker(&mFrameMutex);
    frame = mLatestFrame;
  }

  QPainter painter(this);
  // Always fill first: QOpenGLWidget shows whatever was in its buffer
  // before this call for any pixel paintGL() doesn't touch — with no frame
  // yet, or with a KeepAspectRatio-scaled frame leaving letterbox bars,
  // that used to be a see-through/garbage region instead of a solid
  // background (see this plan's Task 4 / design doc §6).
  painter.fillRect(rect(), Qt::black);
  if (frame.isNull()) {
    return;
  }
  const QRect target = scaledFrameRect(frame.size(), size());
  const QImage scaled = frame.scaled(target.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
  painter.drawImage(target.topLeft(), scaled);
}

} // namespace pcm::video
