#pragma once

#include <QAudioDevice>
#include <QCameraDevice>
#include <QList>
#include <optional>

namespace pcm::video {

// Thin wrapper over QMediaDevices for enumerating and selecting the
// camera/microphone/speaker. Has no dependency on VideoProvider or the
// LiveKit SDK, so device-selection UI is testable independent of any real
// call.
class DeviceManager {
public:
  [[nodiscard]] QList<QCameraDevice> cameras() const;
  [[nodiscard]] QList<QAudioDevice> microphones() const;
  [[nodiscard]] QList<QAudioDevice> speakers() const;

  [[nodiscard]] std::optional<QCameraDevice> defaultCamera() const;
  [[nodiscard]] std::optional<QAudioDevice> defaultMicrophone() const;
  [[nodiscard]] std::optional<QAudioDevice> defaultSpeaker() const;
};

} // namespace pcm::video
