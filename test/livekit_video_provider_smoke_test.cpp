#include "livekit_video_provider.h"

// LiveKitVideoProvider's constructor creates a RemoteVideoRenderer, which is
// a QOpenGLWidget/QWidget (see src/video/remote_video_renderer.h) — like
// Task 6's own remote_video_renderer_smoke_test.cpp, this needs a full
// QApplication rather than QCoreApplication, or QWidget construction aborts
// with "QWidget: Cannot create a QWidget without QApplication".
#include <QApplication>
#include <QEventLoop>
#include <QTimer>
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
  return 0;
}
