// Throwaway spike for issue #77 (LiveKit C++/Qt capture-and-render proof).
// Not shipped, not part of the main app build.
#include <QApplication>

#include <iostream>

#include "livekit/livekit.h"
#include "spike_window.h"

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  livekit::initialize(livekit::LogLevel::Info);
  std::cout << "LiveKit version: " << LIVEKIT_BUILD_VERSION_FULL << std::endl;

  int result = 0;
  {
    // SpikeWindow must be destroyed before livekit::shutdown() runs: its
    // destructor does Room disconnect, track unpublish, and stream close —
    // all LiveKit FFI calls. Scoping it here guarantees ~SpikeWindow() runs
    // at the closing brace, strictly before shutdown() below, rather than at
    // main()'s return (which would be after shutdown() already tore down
    // the SDK).
    SpikeWindow window;
    window.show();
    result = app.exec();
  }

  livekit::shutdown();
  return result;
}
