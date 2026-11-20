#include "remote_video_renderer.h"
#include "video_frame_source.h"

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
    // Qt-only consumption and source destruction are safe even without a
    // paint context. Hardware OpenGL drawing is a separate runtime gate.
    auto source = std::make_unique<pcm::video::VideoFrameSource>();
    renderer->attachSource(source.get());
    QImage frame(16, 9, QImage::Format_RGBA8888);
    frame.fill(Qt::red);
    source->submitFrame(frame);
    app.processEvents();
    source.reset(); // UI survives source destruction and pending notifications.
    app.processEvents();
    renderer->attachSource(nullptr);
    renderer->detach();
    renderer.reset();
  }

  std::cout << "remote_video_renderer_smoke_test: 20 attach/detach/destroy cycles completed"
            << std::endl;
  return 0;
}
