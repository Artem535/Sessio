#include "application.h"
#include "call_scheme_handler.h"
#include "config.h"

int main(const int argc, char *argv[]) {
  // Chromium reads the custom URL scheme table at start-up, before QApplication.
  pcm::webcall::registerCallScheme();
  pcm::config::Config::migrate_legacy_directory();
  const QString launchUrl = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
  return pcm::Application().run(argc, argv, launchUrl);
}
