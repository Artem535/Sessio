#include "device_manager.h"

#include <QMediaDevices>

namespace pcm::video {

DeviceManager::DeviceManager(QObject *parent)
    : QObject(parent), mOwnedMediaDevices(std::make_unique<QMediaDevices>()) {
  connectMediaDevices(mOwnedMediaDevices.get());
}

DeviceManager::DeviceManager(QMediaDevices *mediaDevices, QObject *parent) : QObject(parent) {
  connectMediaDevices(mediaDevices);
}

DeviceManager::~DeviceManager() = default;

void DeviceManager::connectMediaDevices(QMediaDevices *mediaDevices) {
  // QMediaDevices' change notifications are backed by a shared, process-wide
  // platform monitor, so this instance receives them regardless of how many
  // other QMediaDevices instances exist elsewhere in the app.
  connect(mediaDevices, &QMediaDevices::videoInputsChanged, this, &DeviceManager::devicesChanged);
  connect(mediaDevices, &QMediaDevices::audioInputsChanged, this, &DeviceManager::devicesChanged);
  connect(mediaDevices, &QMediaDevices::audioOutputsChanged, this, &DeviceManager::devicesChanged);
}

QList<QCameraDevice> DeviceManager::cameras() const {
  return QMediaDevices::videoInputs();
}

QList<QAudioDevice> DeviceManager::microphones() const {
  return QMediaDevices::audioInputs();
}

QList<QAudioDevice> DeviceManager::speakers() const {
  return QMediaDevices::audioOutputs();
}

std::optional<QCameraDevice> DeviceManager::defaultCamera() const {
  const auto devices = cameras();
  if (devices.isEmpty()) {
    return std::nullopt;
  }
  return devices.first();
}

std::optional<QAudioDevice> DeviceManager::defaultMicrophone() const {
  const auto devices = microphones();
  if (devices.isEmpty()) {
    return std::nullopt;
  }
  return devices.first();
}

std::optional<QAudioDevice> DeviceManager::defaultSpeaker() const {
  const auto devices = speakers();
  if (devices.isEmpty()) {
    return std::nullopt;
  }
  return devices.first();
}

} // namespace pcm::video
