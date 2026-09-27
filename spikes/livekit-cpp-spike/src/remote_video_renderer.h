#pragma once

// Throwaway spike for issue #77 — not production quality.

#include <QImage>
#include <QMutex>
#include <QOpenGLWidget>

#include <atomic>
#include <memory>
#include <thread>

#include "livekit/livekit.h"

// Renders a subscribed remote video track. Runs its own reader thread that
// blocks on livekit::VideoStream::read() (the SDK's frame-delivery API is
// pull-based, not callback-based) and marshals decoded QImages to the Qt
// UI thread for painting.
class RemoteVideoRenderer final : public QOpenGLWidget {
  Q_OBJECT

public:
  explicit RemoteVideoRenderer(QWidget *parent = nullptr);
  ~RemoteVideoRenderer() override;

  void attachTrack(const std::shared_ptr<livekit::Track> &track);
  void detach();

protected:
  void paintGL() override;

private:
  std::shared_ptr<livekit::VideoStream> mStream;
  std::thread mReaderThread;
  std::atomic<bool> mRunning{false};

  QMutex mFrameMutex;
  QImage mLatestFrame;

  void readerLoop();
  void setLatestFrame(const QImage &image);
};
