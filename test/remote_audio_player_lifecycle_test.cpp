#include "remote_audio_player.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <iostream>

namespace pcm::video {
struct RemoteAudioPlayerTestAccess {
  static bool staleDeliveryIsRejected() {
    RemoteAudioPlayer player;
    player.mRunning.store(true);
    const auto oldGeneration = player.mGeneration;
    QMetaObject::invokeMethod(&player, [&player, oldGeneration] {
      player.deliverAudioOnGuiThread(QByteArray(480, '\0'), 48000, 1, oldGeneration);
    }, Qt::QueuedConnection);
    player.detach();
    // Model an immediately attached successor, before queued PCM is processed.
    player.mRunning.store(true);
    QCoreApplication::processEvents();
    const bool rejected = !player.mSink;
    player.detach();
    return rejected;
  }
};
} // namespace pcm::video

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  if (!pcm::video::RemoteAudioPlayerTestAccess::staleDeliveryIsRejected()) {
    std::cerr << "old PCM created the successor audio sink" << std::endl;
    return 1;
  }
  std::cout << "old queued PCM rejected after detach/reattach" << std::endl;
}
