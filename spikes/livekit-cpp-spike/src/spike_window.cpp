// Throwaway spike for issue #77 — not production quality.
#include "spike_window.h"

#include <QHBoxLayout>
#include <QMediaDevices>
#include <QMetaObject>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdlib>
#include <iostream>

SpikeWindow::SpikeWindow(QWidget *parent) : QMainWindow(parent) {
  setWindowTitle("LiveKit C++/Qt Spike");
  resize(960, 640);

  auto *central = new QWidget(this);
  auto *layout = new QVBoxLayout(central);

  mLocalPreview = new QVideoWidget(central);
  mLocalPreview->setMinimumSize(640, 360);
  layout->addWidget(mLocalPreview);

  mRemoteVideo = new RemoteVideoRenderer(central);
  mRemoteVideo->setMinimumSize(640, 360);
  layout->addWidget(mRemoteVideo);

  mStatusLabel = new QLabel(central);
  layout->addWidget(mStatusLabel);

  mConnectionLabel = new QLabel("Not connected.", central);
  layout->addWidget(mConnectionLabel);

  auto *buttonRow = new QWidget(central);
  auto *buttonLayout = new QHBoxLayout(buttonRow);
  mJoinButton = new QPushButton("Join", buttonRow);
  mLeaveButton = new QPushButton("Leave", buttonRow);
  mLeaveButton->setEnabled(false);
  buttonLayout->addWidget(mJoinButton);
  buttonLayout->addWidget(mLeaveButton);
  layout->addWidget(buttonRow);

  auto *deviceRow = new QWidget(central);
  auto *deviceLayout = new QHBoxLayout(deviceRow);

  mCameraCombo = new QComboBox(deviceRow);
  for (const auto &device : QMediaDevices::videoInputs()) {
    mCameraCombo->addItem(device.description(), QVariant::fromValue(device));
  }
  deviceLayout->addWidget(mCameraCombo);

  mMicCombo = new QComboBox(deviceRow);
  for (const auto &device : QMediaDevices::audioInputs()) {
    mMicCombo->addItem(device.description(), QVariant::fromValue(device));
  }
  deviceLayout->addWidget(mMicCombo);

  mSpeakerCombo = new QComboBox(deviceRow);
  for (const auto &device : QMediaDevices::audioOutputs()) {
    mSpeakerCombo->addItem(device.description(), QVariant::fromValue(device));
  }
  deviceLayout->addWidget(mSpeakerCombo);

  layout->addWidget(deviceRow);

  connect(mCameraCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0) return;
    mVideoCapture.start(mCameraCombo->itemData(index).value<QCameraDevice>());
  });
  connect(mMicCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0) return;
    mAudioCapture.start(mMicCombo->itemData(index).value<QAudioDevice>());
  });
  connect(mSpeakerCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
    // Only takes effect if a remote audio track is currently subscribed:
    // re-attaching it tears down and recreates RemoteAudioPlayer's reader
    // thread/QAudioSink against the newly selected device. If no track is
    // attached yet, this just updates which device gets used the next time
    // one subscribes (attachTrack() in onTrackSubscribed reads the combo).
    if (index < 0 || !mRemoteAudioTrack) return;
    mRemoteAudio.attachTrack(mRemoteAudioTrack, mSpeakerCombo->itemData(index).value<QAudioDevice>());
  });

  connect(mJoinButton, &QPushButton::clicked, this, &SpikeWindow::onJoinClicked);
  connect(mLeaveButton, &QPushButton::clicked, this, &SpikeWindow::onLeaveClicked);

  setCentralWidget(central);

  // Show the local capture in this window's own preview, independent of
  // what we push into LiveKit's VideoSource — proves capture works even
  // before Task 4 wires up a room connection.
  //
  // Deliberately NOT calling mLocalPreview->videoSink()->disconnect() here:
  // QVideoWidget wires its own internal repaint slot to its own videoSink()'s
  // videoFrameChanged signal in its constructor. A blanket disconnect() (no
  // args) on that sink severs every outgoing connection from it, including
  // that internal repaint wiring — frames would still flow into the sink via
  // setVideoFrame() below, but the widget would never repaint, so the
  // preview renders blank even while frame counters keep climbing. Qt
  // signals support multiple listeners, so no disconnect is needed: we just
  // add our own frame-forwarding connection alongside the widget's existing
  // internal one.
  connect(mVideoCapture.previewSink(), &QVideoSink::videoFrameChanged,
          mLocalPreview->videoSink(), &QVideoSink::setVideoFrame);

  // The adapters increment their own atomic counters on every frame/chunk
  // (~30/s video, ~100/s audio) regardless of whether anything is connected
  // to frameCaptured — that part is already cheap. What used to be expensive
  // was repainting mStatusLabel synchronously on every single signal
  // (~130 GUI-thread updates/sec). Repaint from a ~1Hz timer instead; the
  // counters it reads are still the live cumulative totals.
  mStatusTimer = new QTimer(this);
  mStatusTimer->setInterval(1000);
  connect(mStatusTimer, &QTimer::timeout, this, &SpikeWindow::updateStatusLabel);
  mStatusTimer->start();

  const auto cameras = QMediaDevices::videoInputs();
  if (!cameras.isEmpty()) {
    mVideoCapture.start(cameras.first());
  } else {
    mStatusLabel->setText("No camera device found.");
  }

  const auto mics = QMediaDevices::audioInputs();
  if (!mics.isEmpty()) {
    mAudioCapture.start(mics.first());
  }

  updateStatusLabel();
}

void SpikeWindow::updateStatusLabel() {
  mStatusLabel->setText(QStringLiteral("Video frames captured: %1   Audio frames captured: %2")
                            .arg(mVideoCapture.framesCaptured())
                            .arg(mAudioCapture.framesCaptured()));
}

SpikeWindow::~SpikeWindow() {
  onLeaveClicked();
  mVideoCapture.stop();
  mAudioCapture.stop();
}

void SpikeWindow::setConnectionState(const QString &text) {
  mConnectionLabel->setText(text);
}

void SpikeWindow::onJoinClicked() {
  const char *url = std::getenv("LIVEKIT_URL");
  const char *token = std::getenv("LIVEKIT_TOKEN");
  if (!url || !token) {
    setConnectionState("LIVEKIT_URL / LIVEKIT_TOKEN not set — cannot join.");
    return;
  }

  mRoom = std::make_unique<livekit::Room>();
  mRoom->setDelegate(this);

  livekit::RoomOptions options;
  options.auto_subscribe = true;
  options.dynacast = false;

  setConnectionState("Connecting...");
  const bool connected = mRoom->connect(url, token, options);
  if (!connected) {
    setConnectionState("Failed to connect.");
    mRoom->setDelegate(nullptr);
    mRoom.reset();
    return;
  }

  setConnectionState("Connected.");
  mJoinButton->setEnabled(false);
  mLeaveButton->setEnabled(true);
  publishTracks();
}

void SpikeWindow::publishTracks() {
  auto lp = mRoom->localParticipant().lock();
  if (!lp) {
    std::cerr << "[room] local participant unavailable, cannot publish" << std::endl;
    return;
  }

  mAudioTrack = livekit::LocalAudioTrack::createLocalAudioTrack("mic", mAudioCapture.audioSource());
  livekit::TrackPublishOptions audioOpts;
  audioOpts.source = livekit::TrackSource::SOURCE_MICROPHONE;
  audioOpts.dtx = false;
  audioOpts.simulcast = false;
  try {
    lp->publishTrack(mAudioTrack, audioOpts);
  } catch (const std::exception &e) {
    std::cerr << "[room] failed to publish audio track: " << e.what() << std::endl;
  }

  mVideoTrack = livekit::LocalVideoTrack::createLocalVideoTrack("cam", mVideoCapture.videoSource());
  livekit::TrackPublishOptions videoOpts;
  videoOpts.source = livekit::TrackSource::SOURCE_CAMERA;
  videoOpts.dtx = false;
  videoOpts.simulcast = true;
  try {
    lp->publishTrack(mVideoTrack, videoOpts);
  } catch (const std::exception &e) {
    std::cerr << "[room] failed to publish video track: " << e.what() << std::endl;
  }
}

void SpikeWindow::unpublishTracks() {
  if (mRoom) {
    if (auto lp = mRoom->localParticipant().lock()) {
      if (mAudioTrack && mAudioTrack->publication()) {
        lp->unpublishTrack(mAudioTrack->publication()->sid());
      }
      if (mVideoTrack && mVideoTrack->publication()) {
        lp->unpublishTrack(mVideoTrack->publication()->sid());
      }
    }
  }
  mAudioTrack.reset();
  mVideoTrack.reset();
}

void SpikeWindow::onLeaveClicked() {
  mRemoteVideo->detach();
  mRemoteAudio.detach();
  mRemoteAudioTrack.reset();

  if (mRoom) {
    unpublishTracks();
    mRoom->setDelegate(nullptr);
    mRoom.reset();
  }

  mJoinButton->setEnabled(true);
  mLeaveButton->setEnabled(false);
  setConnectionState("Not connected.");
}

void SpikeWindow::onParticipantConnected(livekit::Room & /*room*/,
                                         const livekit::ParticipantConnectedEvent &ev) {
  const QString identity = ev.participant ? QString::fromStdString(ev.participant->identity())
                                          : QStringLiteral("<unknown>");
  QMetaObject::invokeMethod(
      this,
      [this, identity]() {
        // Leave() may have already reset mRoom by the time this queued
        // lambda runs (it was posted from LiveKit's thread, decoupled from
        // when the GUI thread actually executes it) — no-op if so.
        if (!mRoom) return;
        setConnectionState(QStringLiteral("Connected. Participant joined: %1").arg(identity));
      },
      Qt::QueuedConnection);
}

void SpikeWindow::onTrackSubscribed(livekit::Room & /*room*/,
                                    const livekit::TrackSubscribedEvent &ev) {
  if (!ev.track) {
    return;
  }
  const auto kind = ev.track->kind();
  auto track = ev.track;

  QMetaObject::invokeMethod(
      this,
      [this, track, kind]() {
        // Leave() may have already reset mRoom by the time this queued
        // lambda runs (posted from LiveKit's thread, decoupled from when the
        // GUI thread actually executes it) — no-op if so, otherwise we'd
        // attach a track onto renderers/players whose Room is already gone.
        if (!mRoom) return;
        if (kind == livekit::TrackKind::KIND_VIDEO) {
          mRemoteVideo->attachTrack(track);
          setConnectionState("Connected. Receiving remote video.");
        } else if (kind == livekit::TrackKind::KIND_AUDIO) {
          // Remembered so the speaker combo can re-attach this same track to
          // a newly selected output device later (see mSpeakerCombo's
          // currentIndexChanged handler below).
          mRemoteAudioTrack = track;
          const auto selected = mSpeakerCombo->currentData();
          const auto outputs = QMediaDevices::audioOutputs();
          const auto device = selected.isValid() ? selected.value<QAudioDevice>()
                                                  : (outputs.isEmpty() ? QAudioDevice() : outputs.first());
          mRemoteAudio.attachTrack(track, device);
        }
      },
      Qt::QueuedConnection);
}
