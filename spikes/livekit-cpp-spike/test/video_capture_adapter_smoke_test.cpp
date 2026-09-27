// Throwaway spike for issue #77 — not production quality.
//
// Headless smoke test for VideoCaptureAdapter's worker-thread machinery
// (added alongside the GUI-thread -> worker-thread capture fix). Not a
// GoogleTest: driving VideoCaptureAdapter's start()/stop()/destroy cycle
// needs a running Qt event loop — the worker thread's queued processFrame()
// calls, and QThread::quit()/wait() making sense at all — which doesn't fit
// gtest's own main(). This is a small standalone program instead.
//
// It does NOT prove frames are captured correctly: there is no camera in
// this sandboxed/CI environment, so no real QVideoFrame ever reaches
// onVideoFrameChanged here. What it does exercise is repeated start()/
// stop()/destroy cycles of the worker thread with nothing (or an invalid
// device) attached, to catch a hang or crash in the shutdown path — exactly
// the class of bug an earlier review round of this spike found in the
// analogous remote-rendering threading code (Task 5).
//
// Exit code 0 = all cycles completed; non-zero = crash or the watchdog timer
// fired, meaning shutdown hung.

#include <QCameraDevice>
#include <QCoreApplication>
#include <QMediaDevices>
#include <QTimer>

#include <cstdlib>
#include <iostream>

#include "livekit/livekit.h"

#include "../src/video_capture_adapter.h"

namespace {

void runSmokeTest() {
  // Prefer a real device if one happens to be attached (e.g. run locally on
  // a dev machine), but this must also be safe with none: QCamera with a
  // default-constructed (invalid) QCameraDevice is documented as safe — it
  // just never becomes active. Either way, this test only cares whether the
  // worker-thread machinery survives repeated start()/stop() cycles, not
  // whether frames actually flow.
  const auto cameras = QMediaDevices::videoInputs();
  const QCameraDevice device = cameras.isEmpty() ? QCameraDevice() : cameras.first();

  for (int i = 0; i < 3; ++i) {
    VideoCaptureAdapter adapter;
    adapter.start(device);
    adapter.stop();
    // Second start()/stop() cycle on the same adapter instance: exercises
    // the worker-thread-recreation path inside start() (it always calls
    // stop() first), i.e. a device switch.
    adapter.start(device);
    adapter.stop();
    // Adapter goes out of scope here with the worker already stopped —
    // exercises the destructor when stop() was already called explicitly.
  }

  {
    // One more adapter left running (thread started, never explicitly
    // stopped) so the destructor itself has to stop the worker thread from
    // a "live" state.
    VideoCaptureAdapter adapter;
    adapter.start(device);
  }

  std::cerr << "[smoke test] PASS: all start()/stop()/destroy cycles completed "
               "without hanging or crashing"
            << std::endl;
  QCoreApplication::exit(EXIT_SUCCESS);
}

}  // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);

  // VideoCaptureAdapter's constructor creates a livekit::VideoSource, which
  // is an FFI call into the LiveKit SDK — it requires livekit::initialize()
  // to have run first (same as main.cpp). Mirror main.cpp's init/shutdown
  // bracketing and its comment about why: any object making LiveKit FFI
  // calls must be destroyed before shutdown() runs.
  livekit::initialize(livekit::LogLevel::Info);

  int result = EXIT_SUCCESS;
  {
    // Run the actual test body once the event loop is up — QThread::wait()
    // inside VideoCaptureAdapter::stop() and the worker's queued
    // processFrame() calls both need one running. A watchdog guards against
    // a hang in the shutdown path: if stop()/the destructor ever deadlocks,
    // this process is killed by the timeout (non-zero exit) instead of
    // hanging forever, which is itself the failure signal this test exists
    // to catch.
    QTimer::singleShot(0, &runSmokeTest);
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [] {
      std::cerr << "[smoke test] FAIL: timed out -- worker thread shutdown likely hung"
                << std::endl;
      QCoreApplication::exit(EXIT_FAILURE);
    });
    watchdog.start(10000);

    result = app.exec();
  }

  livekit::shutdown();
  return result;
}
