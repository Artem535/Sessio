#include "audio_capture_adapter.h"

#include <QCoreApplication>
#include <QMediaDevices>
#include <QTimer>
#include <iostream>
#include <livekit/livekit.h>

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);

  // AudioCaptureAdapter's mAudioSource default member initializer makes an
  // FFI call (constructing a livekit::AudioSource) at construction time,
  // which requires the LiveKit runtime to already be initialized — see the
  // identical note in Task 4's video_capture_adapter_smoke_test.cpp. In
  // production this is LiveKitVideoProvider's LiveKitRuntimeGuard's job
  // (Task 7); this standalone test owns it directly instead.
  livekit::initialize(livekit::LogLevel::Info);

  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, [] {
    std::cerr << "audio_capture_adapter_smoke_test: watchdog fired, hang detected" << std::endl;
    std::exit(1);
  });
  watchdog.start(10000);

  const auto devices = QMediaDevices::audioInputs();

  for (int cycle = 0; cycle < 20; ++cycle) {
    auto adapter = std::make_unique<pcm::video::AudioCaptureAdapter>();
    if (!devices.isEmpty()) {
      adapter->start(devices.first());
    }
    adapter.reset();
  }

  std::cout << "audio_capture_adapter_smoke_test: 20 start/destroy cycles completed" << std::endl;
  livekit::shutdown();
  return 0;
}
