#pragma once

#include "device_manager.h"
#include "audio_check.h"
#include "video_capture_adapter.h"

#include <QBuffer>
#include <QByteArray>
#include <QLabel>
#include <QWidget>
#include <memory>
#include <optional>

class QAudioSink;
class QAudioSource;
class QComboBox;
class QPushButton;

// The "prejoin check" screen: lets the user pick their camera, microphone
// and speaker and see a live preview of the selected camera before joining
// a call. Depends only on pcm::video::DeviceManager/VideoCaptureAdapter —
// no dependency on pcm::database::Database or ClientNotesPage, per the
// call-UI module boundary.
class DeviceCheckWidget final : public QWidget {
  Q_OBJECT

public:
  explicit DeviceCheckWidget(pcm::video::DeviceManager *deviceManager, QWidget *parent = nullptr);
  ~DeviceCheckWidget() override;

  // What the user has picked, or nullopt when there is nothing to pick. Applied to the
  // provider before joining, so the call opens exactly these devices.
  [[nodiscard]] std::optional<QCameraDevice> selectedCamera() const;
  [[nodiscard]] std::optional<QAudioDevice> selectedMicrophone() const;
  [[nodiscard]] std::optional<QAudioDevice> selectedSpeaker() const;

signals:
  void joinRequested();
  void backRequested();

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  // Microphone level meter: listens to the selected microphone only while the screen is shown.
  void restartMicMeter();
  void stopMicMeter();
  // The "Test" button: a short tone through the selected speaker.
  void playSpeakerTest();
  void stopSpeakerTest();
  void ensurePreviewAdapter();
  void restartPreview();
  // Repopulates all three combo boxes from a fresh DeviceManager query,
  // preserving the current selection (matched by QCameraDevice::id()/
  // QAudioDevice::id(), the SDK's documented stable-but-not-human-readable
  // identifier -- unlike description(), which could theoretically collide
  // for two same-model devices) when that device is still present, instead
  // of always resetting to index 0. Connected to DeviceManager::
  // devicesChanged() and also called defensively from showEvent(), in case a
  // device change happened while this widget did not exist or was hidden.
  void refreshDeviceLists();

  pcm::video::DeviceManager *mDeviceManager;
  std::unique_ptr<pcm::video::VideoCaptureAdapter> mPreviewAdapter;
  QComboBox *mCameraCombo{nullptr};
  QComboBox *mMicrophoneCombo{nullptr};
  QComboBox *mSpeakerCombo{nullptr};
  QLabel *mPreviewLabel{nullptr};
  pcm::calls::MicLevelMeter *mMicMeter{nullptr};
  std::unique_ptr<QAudioSource> mMicSource;
  QIODevice *mMicDevice{nullptr};
  std::unique_ptr<QAudioSink> mToneSink;
  QByteArray mToneData;
  QBuffer mToneBuffer;
};
