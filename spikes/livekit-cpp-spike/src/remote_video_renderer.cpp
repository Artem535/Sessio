// Throwaway spike for issue #77 — not production quality.
#include "remote_video_renderer.h"

#include <QMetaObject>
#include <QMutexLocker>
#include <QPainter>

#include <iostream>

RemoteVideoRenderer::RemoteVideoRenderer(QWidget *parent) : QOpenGLWidget(parent) {}

RemoteVideoRenderer::~RemoteVideoRenderer() { detach(); }

void RemoteVideoRenderer::attachTrack(const std::shared_ptr<livekit::Track> &track) {
  detach();
  if (!track) {
    return;
  }

  livekit::VideoStream::Options opts;
  opts.format = livekit::VideoBufferType::RGBA;
  mStream = livekit::VideoStream::fromTrack(track, opts);
  if (!mStream) {
    std::cerr << "[remote video] VideoStream::fromTrack failed" << std::endl;
    return;
  }

  mRunning.store(true);
  mReaderThread = std::thread(&RemoteVideoRenderer::readerLoop, this);
}

void RemoteVideoRenderer::detach() {
  mRunning.store(false);
  if (mStream) {
    mStream->close();  // wakes a blocking read() so the reader thread can exit promptly
  }
  if (mReaderThread.joinable()) {
    mReaderThread.join();
  }
  mStream.reset();
}

void RemoteVideoRenderer::readerLoop() {
  while (mRunning.load()) {
    livekit::VideoFrameEvent vfe;
    if (!mStream->read(vfe)) {
      break;  // EOS / stream closed
    }

    livekit::VideoFrame &frame = vfe.frame;
    if (frame.type() != livekit::VideoBufferType::RGBA) {
      try {
        frame = frame.convert(livekit::VideoBufferType::RGBA, false);
      } catch (const std::exception &e) {
        std::cerr << "[remote video] convert to RGBA failed: " << e.what() << std::endl;
        continue;
      }
    }

    QImage image(frame.data(), frame.width(), frame.height(), QImage::Format_RGBA8888);
    setLatestFrame(image.copy());  // deep copy: frame.data() is only valid for this iteration
  }
}

void RemoteVideoRenderer::setLatestFrame(const QImage &image) {
  {
    QMutexLocker locker(&mFrameMutex);
    mLatestFrame = image;
  }
  QMetaObject::invokeMethod(this, QOverload<>::of(&QOpenGLWidget::update), Qt::QueuedConnection);
}

void RemoteVideoRenderer::paintGL() {
  QImage frame;
  {
    QMutexLocker locker(&mFrameMutex);
    frame = mLatestFrame;
  }
  if (frame.isNull()) {
    return;
  }

  QPainter painter(this);
  const QImage scaled = frame.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
  const QPoint topLeft((width() - scaled.width()) / 2, (height() - scaled.height()) / 2);
  painter.drawImage(topLeft, scaled);
}
