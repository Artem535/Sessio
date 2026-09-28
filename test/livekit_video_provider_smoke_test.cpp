#include "livekit_video_provider.h"

// LiveKitVideoProvider's constructor creates a RemoteVideoRenderer, which is
// a QOpenGLWidget/QWidget (see src/video/remote_video_renderer.h) — like
// Task 6's own remote_video_renderer_smoke_test.cpp, this needs a full
// QApplication rather than QCoreApplication, or QWidget construction aborts
// with "QWidget: Cannot create a QWidget without QApplication".
#include <QApplication>
#include <QEventLoop>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <iostream>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, [] {
    std::cerr << "livekit_video_provider_smoke_test: watchdog fired, hang detected" << std::endl;
    std::exit(1);
  });
  watchdog.start(20000);

  for (int cycle = 0; cycle < 5; ++cycle) {
    auto provider = std::make_unique<pcm::video::LiveKitVideoProvider>();

    // A deliberately unreachable URL: proves join() against a real SDK call
    // that will fail reports joinFailed() rather than hanging, and that a
    // provider can be safely destroyed either mid-attempt or after failure.
    QEventLoop loop;
    QObject::connect(provider.get(), &pcm::video::VideoProvider::joinFailed, &loop, &QEventLoop::quit);
    provider->join("wss://127.0.0.1:1", "not-a-real-token");
    loop.exec();

    provider->leave();
    provider.reset();
  }

  std::cout << "livekit_video_provider_smoke_test: 5 construct/join/destroy cycles completed"
            << std::endl;

  // remoteVideoWidget() ownership: CallPage borrows the provider's renderer
  // by reparenting it into its own layout. Neither destruction order may
  // double-delete it (see ~LiveKitVideoProvider).
  {
    // Provider destroyed first, while its widget is still embedded.
    QWidget host;
    auto *hostLayout = new QVBoxLayout(&host);
    auto provider = std::make_unique<pcm::video::LiveKitVideoProvider>();
    QPointer<QWidget> video = provider->remoteVideoWidget();
    if (video.isNull()) {
      std::cerr << "livekit_video_provider_smoke_test: no remote video widget" << std::endl;
      return 1;
    }
    video->setParent(&host);
    hostLayout->addWidget(video);
    provider.reset();
    if (!video.isNull() || hostLayout->count() != 0) {
      std::cerr << "livekit_video_provider_smoke_test: embedded remote video widget not released "
                   "cleanly with its provider"
                << std::endl;
      return 1;
    }
  } // host destroyed afterwards: must not touch the already-deleted widget
  {
    // Embedding UI destroyed first, then the provider.
    auto provider = std::make_unique<pcm::video::LiveKitVideoProvider>();
    QPointer<QWidget> video = provider->remoteVideoWidget();
    {
      QWidget host;
      auto *hostLayout = new QVBoxLayout(&host);
      video->setParent(&host);
      hostLayout->addWidget(video);
    }
    if (!video.isNull()) {
      std::cerr << "livekit_video_provider_smoke_test: host did not delete its embedded child"
                << std::endl;
      return 1;
    }
    provider.reset(); // must not delete the already-deleted widget again
  }
  std::cout << "livekit_video_provider_smoke_test: remote video widget ownership checks passed"
            << std::endl;
  return 0;
}
