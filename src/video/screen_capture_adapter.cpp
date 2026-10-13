#include "screen_capture_adapter.h"

#include "screen_capture_worker.h"

namespace pcm::video {

ScreenCaptureAdapter::ScreenCaptureAdapter(QObject *parent)
    : QObject(parent), mVideoSource(std::make_shared<livekit::VideoSource>(kWidth, kHeight)) {
  mSession.setVideoSink(&mSink);
  connect(&mSink, &QVideoSink::videoFrameChanged, this, &ScreenCaptureAdapter::onVideoFrameChanged);
}

ScreenCaptureAdapter::~ScreenCaptureAdapter() { stop(); }

bool ScreenCaptureAdapter::start(const ScreenCaptureTarget &target) {
  stop();
  if (!target.isValid()) return false;

  mWorker = std::make_unique<ScreenCaptureWorker>(mVideoSource);
  connect(mWorker.get(), &ScreenCaptureWorker::captureFailed, this, &ScreenCaptureAdapter::captureFailed);
  mWorker->moveToThread(&mWorkerThread);
  mWorkerThread.start();

  if (target.window.isValid()) {
    mWindowCapture = std::make_unique<QWindowCapture>();
    connect(mWindowCapture.get(), &QWindowCapture::errorOccurred, this,
            [this](QWindowCapture::Error, const QString &message) { emit captureFailed(message); });
    mWindowCapture->setWindow(target.window);
    mSession.setWindowCapture(mWindowCapture.get());
    mWindowCapture->start();
  } else {
    mScreenCapture = std::make_unique<QScreenCapture>();
    connect(mScreenCapture.get(), &QScreenCapture::errorOccurred, this,
            [this](QScreenCapture::Error, const QString &message) { emit captureFailed(message); });
    mScreenCapture->setScreen(target.screen);
    mSession.setScreenCapture(mScreenCapture.get());
    mScreenCapture->start();
  }
  mSinceLastFrame.invalidate();
  mActive = true;
  return true;
}

void ScreenCaptureAdapter::stop() {
  if (mScreenCapture) {
    mScreenCapture->stop();
    mSession.setScreenCapture(nullptr);
    mScreenCapture.reset();
  }
  if (mWindowCapture) {
    mWindowCapture->stop();
    mSession.setWindowCapture(nullptr);
    mWindowCapture.reset();
  }
  if (mWorker) {
    mWorker->setRunning(false);
    mWorkerThread.quit();
    mWorkerThread.wait();
    mWorker.reset();
  }
  mActive = false;
}

void ScreenCaptureAdapter::onVideoFrameChanged(const QVideoFrame &frame) {
  if (!mWorker || !frame.isValid() || mWorker->busy()) return;
  if (mSinceLastFrame.isValid() && mSinceLastFrame.elapsed() < kMinFrameIntervalMs) return;
  mSinceLastFrame.restart();
  mWorker->markQueued();
  QMetaObject::invokeMethod(mWorker.get(), "processFrame", Qt::QueuedConnection, Q_ARG(QVideoFrame, frame));
}

} // namespace pcm::video
