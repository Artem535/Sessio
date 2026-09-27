// Throwaway spike for issue #77 — not production quality.
#include "video_capture_adapter.h"

#include <QMediaDevices>
#include <QMetaObject>

#include "video_capture_worker.h"

namespace {
constexpr int kVideoWidth = 1280;
constexpr int kVideoHeight = 720;
}  // namespace

VideoCaptureAdapter::VideoCaptureAdapter(QObject *parent)
    : QObject(parent),
      mVideoSource(std::make_shared<livekit::VideoSource>(kVideoWidth, kVideoHeight)) {
  mSession.setVideoSink(&mSink);
  connect(&mSink, &QVideoSink::videoFrameChanged, this,
          &VideoCaptureAdapter::onVideoFrameChanged);
}

VideoCaptureAdapter::~VideoCaptureAdapter() { stop(); }

void VideoCaptureAdapter::start(const QCameraDevice &device) {
  stop();

  // Fresh worker + worker thread for this capture session. The worker keeps
  // a copy of mVideoSource (kept alive for the adapter's whole lifetime) and
  // a reference to mFramesCaptured (same — never destroyed before the
  // worker is), so both stay valid for as long as the worker exists.
  mWorker = std::make_unique<VideoCaptureWorker>(mVideoSource, mFramesCaptured);
  connect(mWorker.get(), &VideoCaptureWorker::frameCaptured, this,
          &VideoCaptureAdapter::frameCaptured);
  mWorker->moveToThread(&mWorkerThread);
  mWorkerThread.start();

  mCamera = std::make_unique<QCamera>(device, nullptr);
  mSession.setCamera(mCamera.get());
  mCamera->start();
}

void VideoCaptureAdapter::stop() {
  if (mCamera) {
    mCamera->stop();
    mSession.setCamera(nullptr);
    mCamera.reset();
  }

  if (mWorker) {
    // Tell the worker to ignore any processFrame() call still sitting in its
    // thread's queued-event queue, then stop that thread's event loop and
    // wait for it to actually finish before destroying the worker. Only
    // after the thread has stopped is it safe to destroy the worker object
    // it was living on (and, since VideoCaptureAdapter::stop() always runs
    // on the GUI thread, this wait() call is never made from the worker
    // thread itself).
    mWorker->setRunning(false);
    mWorkerThread.quit();
    mWorkerThread.wait();
    mWorker.reset();
  }
}

void VideoCaptureAdapter::onVideoFrameChanged(const QVideoFrame &frame) {
  // Cheap forward only — no conversion or FFI work on the GUI thread. Guard
  // against a frame arriving between mCamera->start() and stop() having torn
  // the worker down (e.g. during a fast device switch).
  if (!mWorker) {
    return;
  }
  QMetaObject::invokeMethod(mWorker.get(), "processFrame", Qt::QueuedConnection,
                             Q_ARG(QVideoFrame, frame));
}
