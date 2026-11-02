#include "screen_capture_adapter.h"

#include "screen_capture_worker.h"

#ifdef SESSIO_HAVE_PIPEWIRE
#include "pipewire_screen_stream.h"
#include "portal_screen_cast.h"
#endif

#include <QGuiApplication>

namespace pcm::video {

struct ScreenCaptureAdapter::PortalCapture {
#ifdef SESSIO_HAVE_PIPEWIRE
  PortalScreenCast portal;
  std::unique_ptr<PipeWireScreenStream> stream;
#endif
};

ScreenCaptureAdapter::ScreenCaptureAdapter(QObject *parent)
    : ScreenCapture(parent), mVideoSource(std::make_shared<livekit::VideoSource>(kWidth, kHeight)) {
  mSession.setVideoSink(&mSink);
  connect(&mSink, &QVideoSink::videoFrameChanged, this, &ScreenCaptureAdapter::onVideoFrameChanged);
}

ScreenCaptureAdapter::~ScreenCaptureAdapter() { stop(); }

bool screenCaptureUsesSystemPicker() {
#ifdef SESSIO_HAVE_PIPEWIRE
  // Qt 6.10's own Wayland capture reuses one portal request path, which some
  // portal backends never release: only the first share of a process worked.
  // An X11 client on a Wayland desktop cannot grab other clients' windows either.
  const QString platform = QGuiApplication::platformName();
  const bool wayland = platform.startsWith(QLatin1String("wayland")) ||
                       (platform == QLatin1String("xcb") && qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland");
  return wayland && PortalScreenCast::isAvailable();
#else
  return false;
#endif
}

bool ScreenCaptureAdapter::start(const ScreenCaptureTarget &target) {
  stop();
  if (!target.isValid()) return false;

  mWorker = std::make_unique<ScreenCaptureWorker>(mVideoSource);
  connect(mWorker.get(), &ScreenCaptureWorker::captureFailed, this, &ScreenCaptureAdapter::reportFailure);
  connect(mWorker.get(), &ScreenCaptureWorker::frameDelivered, this,
          [this, generation = mGeneration] { onFrameDelivered(generation); });
  mWorker->moveToThread(&mWorkerThread);
  mWorkerThread.start();

  mSinceLastFrame.invalidate();
  mStarted = false;
  mActive = true;
  if (screenCaptureUsesSystemPicker())
    startPortalCapture(target);
  else
    startQtCapture(target);
  return true;
}

void ScreenCaptureAdapter::startQtCapture(const ScreenCaptureTarget &target) {
  if (target.window.isValid()) {
    mWindowCapture = std::make_unique<QWindowCapture>(this);
    connect(mWindowCapture.get(), &QWindowCapture::errorOccurred, this,
            [this](QWindowCapture::Error, const QString &message) { reportFailure(message); });
    mWindowCapture->setWindow(target.window);
    mSession.setWindowCapture(mWindowCapture.get());
    mWindowCapture->start();
  } else {
    mScreenCapture = std::make_unique<QScreenCapture>(this);
    connect(mScreenCapture.get(), &QScreenCapture::errorOccurred, this,
            [this](QScreenCapture::Error, const QString &message) { reportFailure(message); });
    mScreenCapture->setScreen(target.screen);
    mSession.setScreenCapture(mScreenCapture.get());
    mScreenCapture->start();
  }
}

void ScreenCaptureAdapter::startPortalCapture(const ScreenCaptureTarget &target) {
  // The desktop's picker offers screens and windows; our request only asked to share.
  Q_UNUSED(target);
#ifdef SESSIO_HAVE_PIPEWIRE
  mPortal = std::make_unique<PortalCapture>();
  PortalScreenCast &portal = mPortal->portal;
  connect(&portal, &PortalScreenCast::failed, this, &ScreenCaptureAdapter::reportFailure);
  connect(&portal, &PortalScreenCast::closed, this,
          [this] { reportFailure(QStringLiteral("The desktop ended screen sharing.")); });
  connect(&portal, &PortalScreenCast::streamReady, this, [this](int fd, quint32 nodeId) {
    const quint64 generation = mGeneration;
    // Both handlers run on PipeWire's thread; hop to ours, dropping a replaced capture's.
    mPortal->stream = std::make_unique<PipeWireScreenStream>(
        [this, generation](QImage image) {
          QMetaObject::invokeMethod(this, [this, generation, image = std::move(image)] {
            if (generation == mGeneration) onVideoFrameChanged(QVideoFrame(image));
          }, Qt::QueuedConnection);
        },
        [this, generation](QString message) {
          QMetaObject::invokeMethod(this, [this, generation, message] {
            if (generation == mGeneration) reportFailure(message);
          }, Qt::QueuedConnection);
        },
        kMinFrameIntervalMs);
    QString error;
    if (!mPortal->stream->open(fd, nodeId, &error)) reportFailure(error);
  });
  portal.start(PortalScreenCast::Monitor | PortalScreenCast::Window);
#endif
}

void ScreenCaptureAdapter::reportFailure(const QString &message) {
  // Qt emits errorOccurred from inside the capture's own code (on Wayland, a
  // portal D-Bus reply). Our listeners destroy the capture on failure, so
  // deliver it once Qt has returned; a capture replaced meanwhile is ignored.
  QMetaObject::invokeMethod(this, [this, message, generation = mGeneration] {
    if (generation == mGeneration && mActive) emit captureFailed(message);
  }, Qt::QueuedConnection);
}

void ScreenCaptureAdapter::onFrameDelivered(quint64 generation) {
  if (generation != mGeneration || !mActive || mStarted) return;
  mStarted = true;
  emit started();
}

void ScreenCaptureAdapter::stop() {
  ++mGeneration;
#ifdef SESSIO_HAVE_PIPEWIRE
  if (mPortal) {
    // Stream first (joins PipeWire's thread), then the portal session it came from.
    if (mPortal->stream) mPortal->stream->close();
    mPortal->portal.disconnect(this);
    mPortal->portal.close();
    mPortal.reset();
  }
#endif
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
  mStarted = false;
}

void ScreenCaptureAdapter::onVideoFrameChanged(const QVideoFrame &frame) {
  if (!mWorker || !frame.isValid() || mWorker->busy()) return;
  if (mSinceLastFrame.isValid() && mSinceLastFrame.elapsed() < kMinFrameIntervalMs) return;
  mSinceLastFrame.restart();
  mWorker->markQueued();
  QMetaObject::invokeMethod(mWorker.get(), "processFrame", Qt::QueuedConnection, Q_ARG(QVideoFrame, frame));
}

} // namespace pcm::video
