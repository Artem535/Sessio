#include "application.h"
#include "config.h"

int main(const int argc, char *argv[]) {
  pcm::config::Config::migrate_legacy_directory();
  const QString launchUrl = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
  return pcm::Application().run(argc, argv, launchUrl);
}