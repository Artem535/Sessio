#include "device_check_widget.h"

#include <QComboBox>
#include <QImage>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QVideoFrame>

namespace {
// Repopulates `combo` from `devices`, keeping whichever device (identified
// by its stable id()) was previously selected, if it is still present.
// Falls back to the first device when the previous selection is gone (or
// there was none yet), rather than leaving the combo on a stale/invalid
// index. Wrapped in a QSignalBlocker so intermediate clear()/addItem() calls
// don't each trigger currentIndexChanged (e.g. DeviceCheckWidget's own
// restartPreview() connection) -- the caller re-syncs once after all three
// combos are refreshed.
template <typename DeviceList>
void refreshDeviceCombo(QComboBox *combo, const DeviceList &devices) {
  const QByteArray previousId = combo->currentData().toByteArray();
  const QSignalBlocker blocker(combo);
  combo->clear();
  int matchIndex = -1;
  for (const auto &device : devices) {
    combo->addItem(device.description(), device.id());
    if (!previousId.isEmpty() && device.id() == previousId) {
      matchIndex = combo->count() - 1;
    }
  }
  if (matchIndex >= 0) {
    combo->setCurrentIndex(matchIndex);
  } else if (combo->count() > 0) {
    combo->setCurrentIndex(0);
  }
}
} // namespace

DeviceCheckWidget::DeviceCheckWidget(pcm::video::DeviceManager *deviceManager, QWidget *parent)
    : QWidget(parent), mDeviceManager(deviceManager) {
  // mPreviewAdapter is intentionally NOT created here. Its real type,
  // pcm::video::VideoCaptureAdapter, builds a livekit::VideoSource in its
  // constructor, which makes an FFI call that requires livekit::initialize()
  // to already have run once, process-wide (see the comment in
  // video_capture_adapter_smoke_test.cpp). That precondition is the running
  // app's responsibility (LiveKitVideoProvider), established long before any
  // call screen is shown -- constructing the adapter eagerly here would force
  // every DeviceCheckWidget construction (including plain selector-UI tests
  // that never call show()) to depend on that global LiveKit setup. Instead
  // the adapter is created lazily in ensurePreviewAdapter(), on first
  // showEvent().
  auto *layout = new QVBoxLayout(this);

  mPreviewLabel = new QLabel(this);
  mPreviewLabel->setMinimumSize(320, 180);
  layout->addWidget(mPreviewLabel);

  mCameraCombo = new QComboBox(this);
  mCameraCombo->setObjectName("cameraCombo");
  for (const auto &camera : mDeviceManager->cameras()) {
    mCameraCombo->addItem(camera.description(), camera.id());
  }
  mMicrophoneCombo = new QComboBox(this);
  mMicrophoneCombo->setObjectName("microphoneCombo");
  for (const auto &mic : mDeviceManager->microphones()) {
    mMicrophoneCombo->addItem(mic.description(), mic.id());
  }
  mSpeakerCombo = new QComboBox(this);
  mSpeakerCombo->setObjectName("speakerCombo");
  for (const auto &speaker : mDeviceManager->speakers()) {
    mSpeakerCombo->addItem(speaker.description(), speaker.id());
  }
  layout->addWidget(mCameraCombo);
  layout->addWidget(mMicrophoneCombo);
  layout->addWidget(mSpeakerCombo);

  auto *backButton = new QPushButton(tr("Back"), this);
  backButton->setObjectName("backFromDeviceCheckButton");
  connect(backButton, &QPushButton::clicked, this, &DeviceCheckWidget::backRequested);
  layout->addWidget(backButton);

  auto *joinButton = new QPushButton(tr("Join"), this);
  joinButton->setObjectName("joinButton");
  connect(joinButton, &QPushButton::clicked, this, &DeviceCheckWidget::joinRequested);
  layout->addWidget(joinButton);

  connect(mCameraCombo, &QComboBox::currentIndexChanged, this, &DeviceCheckWidget::restartPreview);
  // Bug (fixwave userfeedback group A, bug 3): the device lists were only
  // ever populated once, here in the constructor -- plugging in headphones
  // or enabling a camera after this widget existed never showed up without
  // an app restart. DeviceManager::devicesChanged() now fires on every such
  // OS-level change; refresh in response.
  connect(mDeviceManager, &pcm::video::DeviceManager::devicesChanged, this,
          &DeviceCheckWidget::refreshDeviceLists);
}

DeviceCheckWidget::~DeviceCheckWidget() {
  if (mPreviewAdapter) {
    mPreviewAdapter->stop();
  }
}

void DeviceCheckWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  ensurePreviewAdapter();
  // Defensive belt-and-suspenders re-sync: a device change could have
  // happened while this widget didn't exist yet or wasn't visible (e.g.
  // before the very first call), so its combo boxes could otherwise miss
  // that devicesChanged() signal entirely. refreshDeviceLists() ends by
  // calling restartPreview() itself, so no separate call is needed here.
  refreshDeviceLists();
}

void DeviceCheckWidget::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  if (mPreviewAdapter) {
    mPreviewAdapter->stop();
  }
}

void DeviceCheckWidget::ensurePreviewAdapter() {
  if (mPreviewAdapter) {
    return;
  }
  mPreviewAdapter = std::make_unique<pcm::video::VideoCaptureAdapter>();
  connect(mPreviewAdapter->previewSink(), &QVideoSink::videoFrameChanged, this,
          [this](const QVideoFrame &frame) {
            if (frame.isValid()) {
              mPreviewLabel->setPixmap(QPixmap::fromImage(frame.toImage())
                                           .scaled(mPreviewLabel->size(), Qt::KeepAspectRatio));
            }
          });
}

void DeviceCheckWidget::restartPreview() {
  if (!mPreviewAdapter) {
    return;
  }
  const auto cameras = mDeviceManager->cameras();
  const auto index = mCameraCombo->currentIndex();
  if (index < 0 || index >= cameras.size()) {
    return;
  }
  mPreviewAdapter->start(cameras.at(index));
}

void DeviceCheckWidget::refreshDeviceLists() {
  refreshDeviceCombo(mCameraCombo, mDeviceManager->cameras());
  refreshDeviceCombo(mMicrophoneCombo, mDeviceManager->microphones());
  refreshDeviceCombo(mSpeakerCombo, mDeviceManager->speakers());
  // The camera selection may have changed (a new default index, or the
  // previously selected camera vanishing) -- restart the preview against
  // whatever mCameraCombo now points at. No-op if no preview is running yet.
  restartPreview();
}
