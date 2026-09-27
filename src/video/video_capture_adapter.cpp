#include "video_capture_adapter.h"

#include "video_capture_worker.h"

namespace pcm::video {

VideoCaptureAdapter::VideoCaptureAdapter(QObject *parent)
    : QObject(parent),
      mVideoSource(std::make_shared<livekit::VideoSource>(kVideoWidth, kVideoHeight)) {
  mSession.setVideoSink(&mSink);
  connect(&mSink, &QVideoSink::videoFrameChanged, this,
          &VideoCaptureAdapter::onVideoFrameChanged);
}

VideoCaptureAdapter::~VideoCaptureAdapter() {
  stop();
}

void VideoCaptureAdapter::start(const QCameraDevice &device) {
  stop();

  mCamera = std::make_unique<QCamera>(device);
  mSession.setCamera(mCamera.get());

  mWorker = std::make_unique<VideoCaptureWorker>(mVideoSource, mFramesCaptured);
  connect(mWorker.get(), &VideoCaptureWorker::frameCaptured, this,
          &VideoCaptureAdapter::frameCaptured);
  connect(mWorker.get(), &VideoCaptureWorker::captureFailed, this,
          &VideoCaptureAdapter::captureFailed);
  mWorker->moveToThread(&mWorkerThread);
  mWorkerThread.start();

  mCamera->start();
}

void VideoCaptureAdapter::stop() {
  if (mCamera) {
    mCamera->stop();
    mSession.setCamera(nullptr);
    mCamera.reset();
  }

  if (mWorker) {
    mWorker->setRunning(false);
    mWorkerThread.quit();
    mWorkerThread.wait();
    mWorker.reset();
  }
}

void VideoCaptureAdapter::onVideoFrameChanged(const QVideoFrame &frame) {
  if (!mWorker) {
    return;
  }
  QMetaObject::invokeMethod(mWorker.get(), "processFrame", Qt::QueuedConnection,
                            Q_ARG(QVideoFrame, frame));
}

} // namespace pcm::video
