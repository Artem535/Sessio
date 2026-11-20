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

// The operating system's own default is what a user expects a call to open
// (e.g. their connected headset), not whichever device happens to be listed
// first. Fall back to the first listed device only when the platform reports no
// default at all.
std::optional<QCameraDevice> DeviceManager::defaultCamera() const {
  if (const auto device = QMediaDevices::defaultVideoInput(); !device.isNull()) {
    return device;
  }
  const auto devices = cameras();
  if (devices.isEmpty()) {
    return std::nullopt;
  }
  return devices.first();
}

std::optional<QAudioDevice> DeviceManager::defaultMicrophone() const {
  if (const auto device = QMediaDevices::defaultAudioInput(); !device.isNull()) {
    return device;
  }
  const auto devices = microphones();
  if (devices.isEmpty()) {
    return std::nullopt;
  }
  return devices.first();
}

std::optional<QAudioDevice> DeviceManager::defaultSpeaker() const {
  if (const auto device = QMediaDevices::defaultAudioOutput(); !device.isNull()) {
    return device;
  }
  const auto devices = speakers();
  if (devices.isEmpty()) {
    return std::nullopt;
  }
  return devices.first();
}

} // namespace pcm::video
