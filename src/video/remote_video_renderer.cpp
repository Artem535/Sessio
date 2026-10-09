#include "remote_video_renderer.h"

#include <QPainter>

namespace pcm::video {

RemoteVideoRenderer::RemoteVideoRenderer(QWidget *parent) : QOpenGLWidget(parent) {}

void RemoteVideoRenderer::attachSource(VideoFrameSource *source) {
  detach();
  mSource = source;
  if (source) {
    mFrameConnection = connect(source, &VideoFrameSource::frameAvailable, this,
                               QOverload<>::of(&QOpenGLWidget::update));
    mDestroyedConnection = connect(source, &QObject::destroyed, this, [this] { update(); });
  }
  update();
}

void RemoteVideoRenderer::detach() {
  disconnect(mFrameConnection);
  disconnect(mDestroyedConnection);
  mSource.clear();
  update();
}

QRect RemoteVideoRenderer::scaledFrameRect(const QSize &frameSize, const QSize &widgetSize) {
  const QSize scaled = frameSize.scaled(widgetSize, Qt::KeepAspectRatio);
  return QRect(QPoint((widgetSize.width() - scaled.width()) / 2,
                      (widgetSize.height() - scaled.height()) / 2), scaled);
}

void RemoteVideoRenderer::paintGL() {
  const QImage frame = mSource ? mSource->latestFrame() : QImage{};
  QPainter painter(this);
  painter.fillRect(rect(), Qt::black);
  if (!frame.isNull()) {
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(scaledFrameRect(frame.size(), size()), frame);
  }
}

} // namespace pcm::video
