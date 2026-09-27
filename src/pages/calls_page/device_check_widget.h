#pragma once

#include "device_manager.h"
#include "video_capture_adapter.h"

#include <QLabel>
#include <QWidget>
#include <memory>

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

signals:
  void joinRequested();

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  void ensurePreviewAdapter();
  void restartPreview();

  pcm::video::DeviceManager *mDeviceManager;
  std::unique_ptr<pcm::video::VideoCaptureAdapter> mPreviewAdapter;
  QComboBox *mCameraCombo{nullptr};
  QComboBox *mMicrophoneCombo{nullptr};
  QComboBox *mSpeakerCombo{nullptr};
  QLabel *mPreviewLabel{nullptr};
};
