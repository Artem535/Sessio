#pragma once

#include <QMutex>
#include <QOpenGLWidget>
#include <QRect>
#include <QSize>
#include <atomic>
#include <livekit/video_stream.h>
#include <memory>
#include <thread>

namespace pcm::video {

// Renders a subscribed remote LiveKit video track. Pulls frames on a
// dedicated std::thread (blocking VideoStream::read()) and repaints via
// plain QPainter — deliberately not hand-written GL texture upload, which
// would be a large scope increase for no measured benefit yet.
class RemoteVideoRenderer final : public QOpenGLWidget {
  Q_OBJECT
public:
  explicit RemoteVideoRenderer(QWidget *parent = nullptr);
  ~RemoteVideoRenderer() override;

  void attachTrack(const std::shared_ptr<livekit::Track> &track);
  void detach();

protected:
  void paintGL() override;

public:
  // Pure geometry helper (no GL/paint dependency) — where a frame of
  // `frameSize`, scaled with Qt::KeepAspectRatio, lands inside a widget of
  // `widgetSize`. Exposed only so paintGL()'s letterbox math is directly
  // testable without an OpenGL context.
  static QRect scaledFrameRect(const QSize &frameSize, const QSize &widgetSize);

private:
  std::shared_ptr<livekit::VideoStream> mStream;
  std::thread mReaderThread;
  std::atomic<bool> mRunning{false};
  QMutex mFrameMutex;
  QImage mLatestFrame;

  void readerLoop();
  void setLatestFrame(const QImage &image);
};

} // namespace pcm::video
