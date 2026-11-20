// Manual check on a real desktop (not run by ctest): shares the screen
// several times in one process through the production ScreenCaptureAdapter,
// stopping in between. On Wayland every round opens the desktop's picker.
//
//   scripts/run-dev-isolated.sh build/test/Sessio_screen_share_portal_probe [rounds]
//
// Before the portal client existed only round 1 could work under
// xdg-desktop-portal-hyprland; every later round failed with
// "Failed to register object vtable (File exists)".
#include "screen_capture_adapter.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QTimer>
#include <cstdio>
#include <livekit/livekit.h>

using pcm::video::ScreenCaptureAdapter;

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  const int rounds = argc > 1 ? QString::fromLocal8Bit(argv[1]).toInt() : 2;
  livekit::initialize(livekit::LogLevel::Warn);
  std::printf("platform=%s system picker=%s\n", qPrintable(QGuiApplication::platformName()),
              pcm::video::screenCaptureUsesSystemPicker() ? "yes (portal + PipeWire)" : "no (Qt Multimedia)");

  int passed = 0;
  {
    ScreenCaptureAdapter adapter;
    for (int round = 1; round <= rounds; ++round) {
      std::printf("round %d: choose a screen in the picker...\n", round);
      std::fflush(stdout);
      QEventLoop loop;
      QString outcome;
      QElapsedTimer clock;
      clock.start();
      auto started = QObject::connect(&adapter, &ScreenCaptureAdapter::started, &loop, [&] {
        outcome = QStringLiteral("first frame after %1 ms").arg(clock.elapsed());
        ++passed;
        // Keep streaming a moment, as a real share would.
        QTimer::singleShot(2000, &loop, &QEventLoop::quit);
      });
      auto failed = QObject::connect(&adapter, &ScreenCaptureAdapter::captureFailed, &loop,
                                     [&](const QString &reason) {
                                       outcome = QStringLiteral("FAILED: %1").arg(reason);
                                       loop.quit();
                                     });
      QTimer::singleShot(180000, &loop, [&] {
        outcome = QStringLiteral("FAILED: no frame within 180 s");
        loop.quit();
      });
      if (!adapter.start({QGuiApplication::primaryScreen(), {}})) outcome = QStringLiteral("FAILED: start refused");
      else loop.exec();
      QObject::disconnect(started);
      QObject::disconnect(failed);
      adapter.stop();
      std::printf("round %d: %s; stopped\n", round, qPrintable(outcome));
      std::fflush(stdout);
    }
  }
  livekit::shutdown();
  std::printf("%d/%d rounds streamed\n", passed, rounds);
  return passed == rounds ? 0 : 1;
}
