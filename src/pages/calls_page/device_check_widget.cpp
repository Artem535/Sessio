#include "device_check_widget.h"
#include "audio_check.h"

#include <QAudioSink>
#include <QAudioSource>
#include <QBuffer>
#include <QComboBox>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <optional>

namespace {
// A softly tinted, bordered rounded panel that paints itself (see the QSS note in the constructor).
class CardFrame final : public QFrame {
public:
  using QFrame::QFrame;

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QColor(255, 255, 255, 26));
    painter.setBrush(QColor(255, 255, 255, 10));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12.0, 12.0);
  }
};

// A label that keeps a 16:9 shape as the screen grows, so the preview scales instead of being
// letterboxed inside a tall box.
class PreviewLabel final : public QLabel {
public:
  using QLabel::QLabel;
  [[nodiscard]] bool hasHeightForWidth() const override { return true; }
  [[nodiscard]] int heightForWidth(int width) const override { return width * 9 / 16; }
  [[nodiscard]] QSize minimumSizeHint() const override { return {360, 203}; }
};

QPixmap roundedPixmap(const QPixmap &source, qreal radius) {
  QPixmap out(source.size());
  out.fill(Qt::transparent);
  QPainter painter(&out);
  painter.setRenderHint(QPainter::Antialiasing);
  QPainterPath clip;
  clip.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(source.size())), radius, radius);
  painter.setClipPath(clip);
  painter.drawPixmap(0, 0, source);
  return out;
}

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
  // Two columns, capped to a comfortable width and centred: the camera preview on the left, a
  // card with the device selectors on the right, Back/Join underneath.
  // No style sheet on this widget or on any ancestor of the combo boxes: with QSS on an ancestor,
  // Qt keeps re-polishing the combos and Qlementine's ComboboxItemViewFilter then recurses
  // (view() -> container ChildAdded -> filter -> view() ...) until the stack overflows. The card
  // paints itself, and styles go only on leaf widgets that have no combo below them.
  auto *outer = new QVBoxLayout(this);
  auto *column = new QWidget(this);
  column->setMaximumWidth(1180);
  auto *layout = new QVBoxLayout(column);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);
  outer->addStretch(1);
  outer->addWidget(column, 0, Qt::AlignHCenter);
  outer->addStretch(1);

  auto *title = new QLabel(tr("Check your devices"), column);
  QFont titleFont = title->font();
  titleFont.setBold(true);
  titleFont.setPointSizeF(titleFont.pointSizeF() * 1.25);
  title->setFont(titleFont);
  layout->addWidget(title);

  auto *body = new QWidget(column);
  body->setObjectName("deviceCheckBody");
  auto *bodyLayout = new QGridLayout(body);
  bodyLayout->setContentsMargins(0, 0, 0, 0);
  bodyLayout->setHorizontalSpacing(16);
  bodyLayout->setVerticalSpacing(8);
  bodyLayout->setColumnStretch(0, 6);
  bodyLayout->setColumnStretch(1, 5);
  layout->addWidget(body);

  // Left: the preview, with a privacy reminder underneath.
  mPreviewLabel = new PreviewLabel(body);
  mPreviewLabel->setObjectName("devicePreviewLabel");
  QSizePolicy previewPolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  previewPolicy.setHeightForWidth(true);
  mPreviewLabel->setSizePolicy(previewPolicy);
  mPreviewLabel->setAlignment(Qt::AlignCenter);
  mPreviewLabel->setStyleSheet(
      "QLabel#devicePreviewLabel { background-color: rgba(255, 255, 255, 0.06); border-radius: 12px; }");
  bodyLayout->addWidget(mPreviewLabel, 0, 0);
  auto *privacyHint = new QLabel(tr("Only you see this preview until you join"), body);
  privacyHint->setStyleSheet("color: rgba(255, 255, 255, 0.55);");
  bodyLayout->addWidget(privacyHint, 1, 0);

  // Right: the selector card.
  auto *card = new CardFrame(body);
  card->setObjectName("deviceCard");
  auto *cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(16, 14, 16, 14);
  cardLayout->setSpacing(6);
  const auto addCaption = [&](const QString &text) {
    auto *label = new QLabel(text, card);
    label->setStyleSheet("color: rgba(255, 255, 255, 0.60);");
    cardLayout->addWidget(label);
  };

  mCameraCombo = new QComboBox(card);
  mCameraCombo->setObjectName("cameraCombo");
  mMicrophoneCombo = new QComboBox(card);
  mMicrophoneCombo->setObjectName("microphoneCombo");
  mSpeakerCombo = new QComboBox(card);
  mSpeakerCombo->setObjectName("speakerCombo");
  // Populated up front (and not just in showEvent) so the screen already reflects the
  // system default devices, and selected*() are meaningful, before it is first shown.
  refreshDeviceLists();

  addCaption(tr("Camera"));
  cardLayout->addWidget(mCameraCombo);
  cardLayout->addSpacing(6);
  addCaption(tr("Microphone"));
  cardLayout->addWidget(mMicrophoneCombo);
  mMicMeter = new pcm::calls::MicLevelMeter(card);
  cardLayout->addWidget(mMicMeter);
  cardLayout->addSpacing(6);
  addCaption(tr("Speaker"));
  auto *speakerRow = new QHBoxLayout();
  speakerRow->setSpacing(8);
  speakerRow->addWidget(mSpeakerCombo, 1);
  auto *testButton = new QPushButton(tr("Test"), card);
  testButton->setObjectName("testSpeakerButton");
  testButton->setToolTip(tr("Play a short tone through the selected speaker"));
  connect(testButton, &QPushButton::clicked, this, &DeviceCheckWidget::playSpeakerTest);
  speakerRow->addWidget(testButton);
  cardLayout->addLayout(speakerRow);
  cardLayout->addStretch();
  bodyLayout->addWidget(card, 0, 1, 2, 1);

  // Bottom: Back and Join, right-aligned.
  auto *buttonRow = new QWidget(column);
  auto *buttonLayout = new QHBoxLayout(buttonRow);
  buttonLayout->setContentsMargins(0, 0, 0, 0);
  buttonLayout->setSpacing(10);
  buttonLayout->addStretch();
  auto *backButton = new QPushButton(tr("Back"), buttonRow);
  backButton->setObjectName("backFromDeviceCheckButton");
  backButton->setFixedHeight(40);
  backButton->setStyleSheet(
      "QPushButton { background-color: rgba(255, 255, 255, 0.10); color: rgba(255, 255, 255, 0.90);"
      " border: none; border-radius: 20px; padding: 0 24px; }"
      "QPushButton:hover { background-color: rgba(255, 255, 255, 0.16); }");
  connect(backButton, &QPushButton::clicked, this, &DeviceCheckWidget::backRequested);
  buttonLayout->addWidget(backButton);
  auto *joinButton = new QPushButton(tr("Join"), buttonRow);
  joinButton->setObjectName("joinButton");
  joinButton->setFixedHeight(40);
  joinButton->setStyleSheet(
      "QPushButton { background-color: rgb(76, 132, 255); color: white; border: none;"
      " border-radius: 20px; padding: 0 28px; font-weight: bold; }"
      "QPushButton:hover { background-color: rgb(98, 148, 255); }");
  connect(joinButton, &QPushButton::clicked, this, &DeviceCheckWidget::joinRequested);
  buttonLayout->addWidget(joinButton);
  layout->addWidget(buttonRow);

  connect(mMicrophoneCombo, &QComboBox::currentIndexChanged, this, &DeviceCheckWidget::restartMicMeter);
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
  stopMicMeter();
  stopSpeakerTest();
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
  // Release the microphone and any test tone as soon as the screen goes away: the call opens
  // its own capture, and the device must be free for it.
  stopMicMeter();
  stopSpeakerTest();
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
              mPreviewLabel->setPixmap(roundedPixmap(
                  QPixmap::fromImage(frame.toImage())
                      .scaled(mPreviewLabel->size(), Qt::KeepAspectRatioByExpanding,
                              Qt::SmoothTransformation),
                  12.0));
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
  // The selected microphone may have changed or vanished too (no-op while hidden).
  restartMicMeter();
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

void DeviceCheckWidget::stopMicMeter() {
  if (mMicDevice) {
    disconnect(mMicDevice, nullptr, this, nullptr);
    mMicDevice = nullptr;
  }
  if (mMicSource) {
    mMicSource->stop();
    mMicSource.reset();
  }
  if (mMicMeter) {
    mMicMeter->reset();
  }
}

void DeviceCheckWidget::restartMicMeter() {
  stopMicMeter();
  if (!isVisible()) {
    return; // only listen while the screen is actually shown
  }
  const auto microphone = selectedMicrophone();
  if (!microphone) {
    return;
  }
  const QAudioFormat format = microphone->preferredFormat();
  mMicSource = std::make_unique<QAudioSource>(*microphone, format, this);
  mMicDevice = mMicSource->start();
  if (!mMicDevice) {
    mMicSource.reset();
    return;
  }
  const auto sampleFormat = format.sampleFormat();
  connect(mMicDevice, &QIODevice::readyRead, this, [this, sampleFormat]() {
    if (mMicDevice) {
      mMicMeter->setLevel(pcm::calls::normalizedPeak(mMicDevice->readAll(), sampleFormat));
    }
  });
}

void DeviceCheckWidget::stopSpeakerTest() {
  if (mToneSink) {
    mToneSink->stop();
    mToneSink.reset();
  }
  mToneBuffer.close();
  mToneData.clear();
}

void DeviceCheckWidget::playSpeakerTest() {
  stopSpeakerTest();
  const auto speaker = selectedSpeaker();
  if (!speaker) {
    return;
  }
  QAudioFormat format;
  format.setSampleRate(48000);
  format.setChannelCount(1);
  format.setSampleFormat(QAudioFormat::Int16);
  if (!speaker->isFormatSupported(format)) {
    format = speaker->preferredFormat();
  }
  mToneData = pcm::calls::makeTestTone(format, 700, 440.0);
  if (mToneData.isEmpty()) {
    return;
  }
  mToneBuffer.setBuffer(&mToneData);
  mToneBuffer.open(QIODevice::ReadOnly);
  mToneSink = std::make_unique<QAudioSink>(*speaker, format, this);
  mToneSink->setVolume(0.6);
  connect(mToneSink.get(), &QAudioSink::stateChanged, this, [this](QAudio::State state) {
    if (state == QAudio::IdleState) {
      // Played to the end: release the speaker. Deferred, because this runs inside the sink.
      QMetaObject::invokeMethod(this, [this]() { stopSpeakerTest(); }, Qt::QueuedConnection);
    }
  });
  mToneSink->start(&mToneBuffer);
}
