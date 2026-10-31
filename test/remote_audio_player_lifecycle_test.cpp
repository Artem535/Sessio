#include "remote_audio_player.h"

#include <QCoreApplication>
#include <QBuffer>
#include <QMediaDevices>
#include <QThread>
#include <QMetaObject>
#include <iostream>

namespace pcm::video {
struct RemoteAudioPlayerTestAccess {
  static bool realOutputSurvivesStop() {
    RemoteAudioPlayer player;
    player.mOutputDevice = QMediaDevices::defaultAudioOutput();
    if (player.mOutputDevice.isNull()) return false;
    player.mRunning.store(true);
    const auto generation = player.mGeneration;
    for (int i = 0; i < 50; ++i) {
      player.deliverAudioOnGuiThread(QByteArray(960, '\0'), 48000, 1, generation);
      QCoreApplication::processEvents();
      QThread::msleep(10);
    }
    if (!player.mSinkDevice || player.mSink->processedUSecs() <= 0) return false;
    QMetaObject::invokeMethod(&player, [&player, generation] {
      player.deliverAudioOnGuiThread(QByteArray(960, '\0'), 48000, 1, generation);
    }, Qt::QueuedConnection);
    player.mSink->stop();
    QCoreApplication::processEvents();
    return player.mSink->state() == QtAudio::StoppedState;
  }

  static bool destroyedOutputIsRejected() {
    RemoteAudioPlayer player;
    player.mRunning.store(true);
    player.mSink = std::make_unique<QAudioSink>();
    auto *output = new QBuffer;
    output->open(QIODevice::WriteOnly);
    player.mSinkDevice = output;
    const auto generation = player.mGeneration;
    player.deliverAudioOnGuiThread(QByteArray(480, '\0'), 48000, 1, generation);
    if (output->data().size() != 480) {
      delete output;
      return false;
    }
    QMetaObject::invokeMethod(&player, [&player, generation] {
      player.deliverAudioOnGuiThread(QByteArray(480, '\0'), 48000, 1, generation);
    }, Qt::QueuedConnection);
    // The backend can destroy its push device while the attachment and queued
    // PCM are still alive. Reject the dangling handle before processing PCM.
    delete output;
    if (player.mSinkDevice) {
      return false;
    }
    QCoreApplication::processEvents();
    return !player.mSinkDevice;
  }

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
  // Explicit hardware probe; regular CTest remains independent of devices.
  if (app.arguments().contains("--output-smoke")) {
    if (!pcm::video::RemoteAudioPlayerTestAccess::realOutputSurvivesStop()) {
      std::cerr << "real audio output start/stop failed" << std::endl;
      return 1;
    }
    std::cout << "real audio output processed silence and survived queued PCM after stop" << std::endl;
  }
  if (!pcm::video::RemoteAudioPlayerTestAccess::destroyedOutputIsRejected()) {
    std::cerr << "destroyed audio output retained a dangling handle" << std::endl;
    return 1;
  }
  if (!pcm::video::RemoteAudioPlayerTestAccess::staleDeliveryIsRejected()) {
    std::cerr << "old PCM created the successor audio sink" << std::endl;
    return 1;
  }
  std::cout << "destroyed output and old queued PCM rejected" << std::endl;
}
