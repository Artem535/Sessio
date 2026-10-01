#include "device_check_widget.h"

#include <QComboBox>
#include <QImage>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <optional>

namespace {
// Repopulates `combo` from `devices`, keeping whichever device (identified
// by its stable id()) was previously selected, if it is still present.
// Falls back to `defaultId` (the operating system's default device) when the
// previous selection is gone or there was none yet, and to the first device
// when even that is unknown, rather than leaving the combo on a stale/invalid
// index. Wrapped in a QSignalBlocker so intermediate clear()/addItem() calls
// don't each trigger currentIndexChanged (e.g. DeviceCheckWidget's own
// restartPreview() connection) -- the caller re-syncs once after all three
// combos are refreshed.
template <typename DeviceList>
void refreshDeviceCombo(QComboBox *combo, const DeviceList &devices, const QByteArray &defaultId) {
  const QByteArray previousId = combo->currentData().toByteArray();
  const QSignalBlocker blocker(combo);
  combo->clear();
  int matchIndex = -1;
  int defaultIndex = -1;
  for (const auto &device : devices) {
    combo->addItem(device.description(), device.id());
    if (!previousId.isEmpty() && device.id() == previousId) {
      matchIndex = combo->count() - 1;
    }
    if (!defaultId.isEmpty() && device.id() == defaultId) {
      defaultIndex = combo->count() - 1;
    }
  }
  if (matchIndex >= 0) {
    combo->setCurrentIndex(matchIndex);
  } else if (defaultIndex >= 0) {
    combo->setCurrentIndex(defaultIndex);
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
  // Content is capped to a comfortable width and centred, so the preview and the selectors form
  // one column instead of stretching edge to edge with the preview stuck to the left.
  auto *outer = new QVBoxLayout(this);
  auto *column = new QWidget(this);
  column->setMaximumWidth(720);
  auto *layout = new QVBoxLayout(column);
  layout->setContentsMargins(0, 0, 0, 0);
  outer->addWidget(column, 0, Qt::AlignHCenter | Qt::AlignTop);

  mPreviewLabel = new QLabel(column);
  mPreviewLabel->setMinimumSize(640, 360); // 16:9, matching the camera feed
  mPreviewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  mPreviewLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(mPreviewLabel);

  const auto addLabeled = [&](const QString &caption, QComboBox *combo) {
    auto *label = new QLabel(caption, column);
    layout->addWidget(label);
    layout->addWidget(combo);
  };

  mCameraCombo = new QComboBox(column);
  mCameraCombo->setObjectName("cameraCombo");
  mMicrophoneCombo = new QComboBox(column);
  mMicrophoneCombo->setObjectName("microphoneCombo");
  mSpeakerCombo = new QComboBox(column);
  mSpeakerCombo->setObjectName("speakerCombo");
  // Populated up front (and not just in showEvent) so the screen already reflects the
  // system default devices, and selected*() are meaningful, before it is first shown.
  refreshDeviceLists();
  addLabeled(tr("Camera"), mCameraCombo);
  addLabeled(tr("Microphone"), mMicrophoneCombo);
  addLabeled(tr("Speaker"), mSpeakerCombo);

  auto *backButton = new QPushButton(tr("Back"), column);
  backButton->setObjectName("backFromDeviceCheckButton");
  connect(backButton, &QPushButton::clicked, this, &DeviceCheckWidget::backRequested);
  layout->addWidget(backButton);

  auto *joinButton = new QPushButton(tr("Join"), column);
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
                                           .scaled(mPreviewLabel->size(), Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation));
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
  const auto defaultCamera = mDeviceManager->defaultCamera();
  const auto defaultMicrophone = mDeviceManager->defaultMicrophone();
  const auto defaultSpeaker = mDeviceManager->defaultSpeaker();
  refreshDeviceCombo(mCameraCombo, mDeviceManager->cameras(),
                     defaultCamera ? defaultCamera->id() : QByteArray());
  refreshDeviceCombo(mMicrophoneCombo, mDeviceManager->microphones(),
                     defaultMicrophone ? defaultMicrophone->id() : QByteArray());
  refreshDeviceCombo(mSpeakerCombo, mDeviceManager->speakers(),
                     defaultSpeaker ? defaultSpeaker->id() : QByteArray());
  // The camera selection may have changed (a new default index, or the
  // previously selected camera vanishing) -- restart the preview against
  // whatever mCameraCombo now points at. No-op if no preview is running yet.
  restartPreview();
}

namespace {
// The device in `devices` whose stable id() is the combo's current selection.
template <typename DeviceList>
auto selectedDevice(const QComboBox *combo, const DeviceList &devices)
    -> std::optional<typename DeviceList::value_type> {
  const QByteArray id = combo->currentData().toByteArray();
  if (id.isEmpty()) {
    return std::nullopt;
  }
  for (const auto &device : devices) {
    if (device.id() == id) {
      return device;
    }
  }
  return std::nullopt;
}
} // namespace

std::optional<QCameraDevice> DeviceCheckWidget::selectedCamera() const {
  return selectedDevice(mCameraCombo, mDeviceManager->cameras());
}

std::optional<QAudioDevice> DeviceCheckWidget::selectedMicrophone() const {
  return selectedDevice(mMicrophoneCombo, mDeviceManager->microphones());
}

std::optional<QAudioDevice> DeviceCheckWidget::selectedSpeaker() const {
  return selectedDevice(mSpeakerCombo, mDeviceManager->speakers());
}
