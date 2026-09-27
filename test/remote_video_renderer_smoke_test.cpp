#include "remote_video_renderer.h"

#include <QApplication>
#include <QTimer>
#include <iostream>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, [] {
    std::cerr << "remote_video_renderer_smoke_test: watchdog fired, hang detected" << std::endl;
    std::exit(1);
  });
  watchdog.start(10000);

  for (int cycle = 0; cycle < 20; ++cycle) {
    auto renderer = std::make_unique<pcm::video::RemoteVideoRenderer>();
    // No real track available in this environment — attachTrack(nullptr) and
    // an immediate detach()/destroy prove the null-track and never-attached
    // paths don't hang or crash, which is what a Leave-before-any-remote-
    // track-ever-subscribed race would exercise in production.
    renderer->attachTrack(nullptr);
    renderer->detach();
    renderer.reset();
  }

  std::cout << "remote_video_renderer_smoke_test: 20 attach/detach/destroy cycles completed"
            << std::endl;
  return 0;
}
