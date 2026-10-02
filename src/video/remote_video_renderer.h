#pragma once

#include "video_frame_source.h"
#include <QOpenGLWidget>
#include <QPointer>
#include <QRect>
#include <QSize>

namespace pcm::video {

// UI-owned Qt consumer; SDK stream ownership stays with the provider.
class RemoteVideoRenderer final : public QOpenGLWidget {
  Q_OBJECT
public:
  explicit RemoteVideoRenderer(QWidget *parent = nullptr);
  void attachSource(VideoFrameSource *source);
  void detach();
  static QRect scaledFrameRect(const QSize &frameSize, const QSize &widgetSize);

protected:
  void paintGL() override;

private:
  QPointer<VideoFrameSource> mSource;
  QMetaObject::Connection mFrameConnection;
  QMetaObject::Connection mDestroyedConnection;
};

} // namespace pcm::video
