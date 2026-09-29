#pragma once

#include <QAudioDevice>
#include <QCameraDevice>
#include <QList>
#include <QObject>

#include <memory>
#include <optional>

class QMediaDevices;

namespace pcm::video {

// Thin wrapper over QMediaDevices for enumerating and selecting the
// camera/microphone/speaker. Has no dependency on VideoProvider or the
// LiveKit SDK, so device-selection UI is testable independent of any real
// call.
//
// QObject-derived so a hot-plugged/removed/enabled device while the app is
// running can be observed via devicesChanged() and used to refresh any UI
// presenting these lists. cameras()/microphones()/speakers()/defaultXxx()
// remain live-queried on every call, exactly as before -- devicesChanged()
// is purely an added notification, not a cache.
class DeviceManager : public QObject {
  Q_OBJECT

public:
  explicit DeviceManager(QObject *parent = nullptr);
  // Test-only: wires devicesChanged() to the given, externally owned
  // QMediaDevices instance instead of one this DeviceManager creates and
  // owns itself. This lets a unit test drive
  // videoInputsChanged()/audioInputsChanged()/audioOutputsChanged() directly
  // (calling a Qt signal is an ordinary, if unusual, public function call
  // since `signals:` compiles down to `public:`) without a real device
  // hot-plug event, which cannot be produced in a sandboxed test
  // environment. `mediaDevices` must outlive this DeviceManager. Production
  // code always uses the constructor above.
  explicit DeviceManager(QMediaDevices *mediaDevices, QObject *parent = nullptr);
  ~DeviceManager() override;

  [[nodiscard]] QList<QCameraDevice> cameras() const;
  [[nodiscard]] QList<QAudioDevice> microphones() const;
  [[nodiscard]] QList<QAudioDevice> speakers() const;

  [[nodiscard]] std::optional<QCameraDevice> defaultCamera() const;
  [[nodiscard]] std::optional<QAudioDevice> defaultMicrophone() const;
  [[nodiscard]] std::optional<QAudioDevice> defaultSpeaker() const;

signals:
  // Fired whenever a camera, microphone, or speaker is plugged in, removed,
  // or enabled/disabled at the OS level while the app is running.
  void devicesChanged();

private:
  void connectMediaDevices(QMediaDevices *mediaDevices);

  // Only set (and owned) when constructed via the public single-argument
  // constructor. The test-injection constructor leaves this null and relies
  // on the caller-owned instance passed in staying alive instead.
  std::unique_ptr<QMediaDevices> mOwnedMediaDevices;
};

} // namespace pcm::video
