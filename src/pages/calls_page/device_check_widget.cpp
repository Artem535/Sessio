#include "device_check_widget.h"

#include <QComboBox>
#include <QImage>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVideoFrame>

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
    mCameraCombo->addItem(camera.description());
  }
  mMicrophoneCombo = new QComboBox(this);
  mMicrophoneCombo->setObjectName("microphoneCombo");
  for (const auto &mic : mDeviceManager->microphones()) {
    mMicrophoneCombo->addItem(mic.description());
  }
  mSpeakerCombo = new QComboBox(this);
  mSpeakerCombo->setObjectName("speakerCombo");
  for (const auto &speaker : mDeviceManager->speakers()) {
    mSpeakerCombo->addItem(speaker.description());
  }
  layout->addWidget(mCameraCombo);
  layout->addWidget(mMicrophoneCombo);
  layout->addWidget(mSpeakerCombo);

  auto *joinButton = new QPushButton(tr("Join"), this);
  joinButton->setObjectName("joinButton");
  connect(joinButton, &QPushButton::clicked, this, &DeviceCheckWidget::joinRequested);
  layout->addWidget(joinButton);

  connect(mCameraCombo, &QComboBox::currentIndexChanged, this, &DeviceCheckWidget::restartPreview);
}

DeviceCheckWidget::~DeviceCheckWidget() {
  if (mPreviewAdapter) {
    mPreviewAdapter->stop();
  }
}

void DeviceCheckWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  ensurePreviewAdapter();
  restartPreview();
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
