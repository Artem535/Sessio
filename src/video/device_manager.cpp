#include "device_manager.h"

#include <QMediaDevices>

namespace pcm::video {

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
