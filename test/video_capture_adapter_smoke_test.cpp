#include "video_capture_adapter.h"

#include <QCoreApplication>
#include <QMediaDevices>
#include <QTimer>
#include <iostream>
#include <livekit/livekit.h>

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);

  // livekit::VideoSource's constructor (invoked by VideoCaptureAdapter) makes
  // an FFI call and requires livekit::initialize() to have already run — per
  // livekit/livekit.h, it "must be the first LiveKit API called in the
  // process." In the full app this will be Task 7's (LiveKitVideoProvider)
  // responsibility via a reference-counted guard; this standalone smoke test
  // has no LiveKitVideoProvider wrapping it, so it owns the call directly,
  // once, for the process's lifetime.
  livekit::initialize(livekit::LogLevel::Info);

  // 10-second watchdog: if repeated start/stop/destroy cycling hangs, this
  // fires and the process exits non-zero instead of hanging CI forever.
  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, [] {
    std::cerr << "video_capture_adapter_smoke_test: watchdog fired, hang detected" << std::endl;
    std::exit(1);
  });
  watchdog.start(10000);

  const auto devices = QMediaDevices::videoInputs();

  for (int cycle = 0; cycle < 20; ++cycle) {
    auto adapter = std::make_unique<pcm::video::VideoCaptureAdapter>();
    if (!devices.isEmpty()) {
      adapter->start(devices.first());
    }
    // Destroy immediately without an explicit stop() — the destructor must
    // tear down the worker thread cleanly on its own, mirroring how
    // LiveKitVideoProvider's own destructor (Task 7) will rely on this.
    adapter.reset();
  }

  std::cout << "video_capture_adapter_smoke_test: 20 start/destroy cycles completed" << std::endl;
  livekit::shutdown();
  return 0;
}
