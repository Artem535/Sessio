#include "livekit_video_provider.h"

#include "audio_capture_adapter.h"
#include "device_manager.h"
#include "remote_audio_player.h"
#include "remote_video_renderer.h"
#include "video_capture_adapter.h"

#include <QLabel>
#include <QMetaObject>
#include <QPixmap>
#include <QVideoFrame>
#include <QVideoSink>
#include <atomic>
#include <chrono>

namespace pcm::video {

namespace {
// Reference-counted so livekit::initialize()/shutdown() run exactly once
// each, regardless of how many LiveKitVideoProvider instances exist over
// the app's lifetime — required because livekit::shutdown() must only run
// after every livekit-holding object has been destroyed (see this plan's
// Task 7 design notes / the spike's main.cpp shutdown-ordering fix,
// commit 6b63162).
std::atomic<int> gLiveKitRefCount{0};

void acquireLiveKitRuntime() {
  if (gLiveKitRefCount.fetch_add(1) == 0) {
    livekit::initialize(livekit::LogLevel::Info);
  }
}

void releaseLiveKitRuntime() {
  if (gLiveKitRefCount.fetch_sub(1) == 1) {
    livekit::shutdown();
  }
}
} // namespace

// Constructed before every other member (see the header's declaration-order
// comment), so livekit::initialize() always runs before mVideoCapture/
// mAudioCapture's constructors make their first LiveKit FFI call.
LiveKitVideoProvider::LiveKitRuntimeGuard::LiveKitRuntimeGuard() {
  acquireLiveKitRuntime();
}

// Destroyed after every other member, so livekit::shutdown() only runs once
// mVideoCapture/mAudioCapture/mRoom/the tracks are already gone.
LiveKitVideoProvider::LiveKitRuntimeGuard::~LiveKitRuntimeGuard() {
  releaseLiveKitRuntime();
}

LiveKitVideoProvider::LiveKitVideoProvider(QObject *parent)
    : VideoProvider(parent), mDeviceManager(std::make_unique<DeviceManager>()),
      mVideoCapture(std::make_unique<VideoCaptureAdapter>()),
      mAudioCapture(std::make_unique<AudioCaptureAdapter>()),
      mRemoteVideo(new RemoteVideoRenderer()),
      mRemoteAudio(std::make_unique<RemoteAudioPlayer>()) {
  // mRuntimeGuard is not listed above: it has no arguments to pass, and its
  // default constructor already ran before this init list's members
  // because of its declaration order in the header (first). Do not add it
  // here — doing so would not change construction order and only invites a
  // future edit that reorders the list and silently breaks the guarantee.

  // Local device failures are never network loss — routed to mediaError(),
  // not connectionLost(), so they cannot drive VideoSession's
  // Connected/Reconnecting/Failed graph (see video_provider.h).
  connect(mVideoCapture.get(), &VideoCaptureAdapter::captureFailed, this,
          &VideoProvider::mediaError);
  connect(mAudioCapture.get(), &AudioCaptureAdapter::captureFailed, this,
          &VideoProvider::mediaError);

  mLocalPreviewWidget = new QLabel();
  mLocalPreviewWidget->setObjectName("localVideoWidget");
  connect(mVideoCapture->previewSink(), &QVideoSink::videoFrameChanged, this,
          [this](const QVideoFrame &frame) {
            if (mLocalPreviewWidget && frame.isValid()) {
              mLocalPreviewWidget->setPixmap(
                  QPixmap::fromImage(frame.toImage())
                      .scaled(mLocalPreviewWidget->size(), Qt::KeepAspectRatio,
                              Qt::SmoothTransformation));
            }
          });

  // Camera/microphone capture starts in join(), not here: starting it at
  // construction time — before any call is joined or even requested — is
  // a privacy problem (the device's capture indicator lights up with no
  // call in progress). See join()/leave() for the actual start/stop.
}

LiveKitVideoProvider::~LiveKitVideoProvider() {
  // leave() already stops both capture adapters — no need to repeat it
  // here (join() is the only place that starts them).
  leave();
  // This provider owns mRemoteVideo for its whole life, even while a UI
  // (CallPage) has reparented it into its own layout via
  // remoteVideoWidget(). Deleting it here unconditionally is safe in every
  // ordering, and is NOT a double delete:
  //  - UI destroyed first: Qt's parent-child cleanup deleted the widget,
  //    which nulled this QPointer, so this is `delete nullptr` (a no-op).
  //  - Provider destroyed first while the widget is still embedded: deleting
  //    a child QWidget is well-defined in Qt — ~QObject detaches it from its
  //    parent's children list (so the parent never deletes it again) and the
  //    parent's layout drops its item on the resulting ChildRemoved event.
  //    The UI holds it only through a QPointer, which nulls too.
  // Deleting only when parent() == nullptr would instead leak it in the
  // second case: the UI never deletes a borrowed widget, it only hands it
  // back with setParent(nullptr) when swapping it out.
  delete mRemoteVideo.data();
  // This provider owns mLocalPreviewWidget for its whole life, even while a
  // UI (CallPage) has reparented it into its own layout via
  // localVideoWidget(). Deleting it here unconditionally is safe in every
  // ordering, and is NOT a double delete:
  //  - UI destroyed first: Qt's parent-child cleanup deleted the widget,
  //    which nulled this QPointer, so this is `delete nullptr` (a no-op).
  //  - Provider destroyed first while the widget is still embedded: deleting
  //    a child QWidget is well-defined in Qt — ~QObject detaches it from its
  //    parent's children list (so the parent never deletes it again) and the
  //    parent's layout drops its item on the resulting ChildRemoved event.
  //    The UI holds it only through a QPointer, which nulls too.
  // Deleting only when parent() == nullptr would instead leak it in the
  // second case: the UI never deletes a borrowed widget, it only hands it
  // back with setParent(nullptr) when swapping it out.
  delete mLocalPreviewWidget.data();
  // No explicit releaseLiveKitRuntime() call here: mRuntimeGuard's own
  // destructor handles it automatically, and — because it is declared
  // first in the header — runs last, after mVideoCapture/mAudioCapture/
  // mRoom/every track member above has already been destroyed by the
  // implicit member-destruction that follows this destructor body. Adding
  // a call here would double-release against the guard's own release.
}

void LiveKitVideoProvider::join(const QString &url, const QString &token) {
  if (const auto camera = mDeviceManager->defaultCamera()) {
    mVideoCapture->start(*camera);
  }
  if (const auto microphone = mDeviceManager->defaultMicrophone()) {
    mAudioCapture->start(*microphone);
  }

  mRoom = std::make_unique<livekit::Room>();
  mRoom->setDelegate(this);

  livekit::RoomOptions options;
  options.auto_subscribe = true;
  options.dynacast = false;
  // The brief's original design left join_retries/connect_timeout unset
  // (Rust SDK defaults). In practice that lets Room::connect() retry a
  // failing initial join for well over 20 seconds even when the transport
  // fails immediately (verified against a deliberately-refused localhost
  // connection: the FFI layer logs the connection-refused error almost
  // instantly, but connect() doesn't return until the retries/backoff are
  // exhausted). That contradicts this class's contract that join() reports
  // joinFailed() promptly rather than hanging, so both are bounded
  // explicitly: no retry of the initial attempt, and a 5s cap per attempt.
  options.join_retries = 0;
  options.connect_timeout = std::chrono::seconds(5);

  const bool connected = mRoom->connect(url.toStdString(), token.toStdString(), options);
  if (!connected) {
    mRoom->setDelegate(nullptr);
    mRoom.reset();
    mVideoCapture->stop();
    mAudioCapture->stop();
    // Room::connect() above is itself a blocking, synchronous SDK call (see
    // the vendored room.h: "Blocks until the FFI connect response arrives").
    // Against a promptly-refused connection it can return in well under a
    // millisecond — before the caller has had any chance to start its event
    // loop. This class's header documents join() as asynchronous ("callers
    // observe the outcome via signals"), and callers (including this
    // class's own smoke test) follow the idiomatic
    //   connect(provider, &VideoProvider::joinFailed, &loop, &QEventLoop::quit);
    //   provider->join(...);
    //   loop.exec();
    // pattern. Emitting joinFailed() directly here would invoke that
    // already-connected slot synchronously, inside join()'s own call frame —
    // i.e. before loop.exec() is entered. QEventLoop::quit() delivered to a
    // loop that isn't running yet has no effect on the future exec() call,
    // so exec() would then block forever. Queuing the emission guarantees
    // it is only delivered once the calling thread's event loop is actually
    // pumping, which is what makes the async contract hold in practice.
    QMetaObject::invokeMethod(
        this,
        [this]() {
          // mRoom is already null at this point (reset just above), so this
          // guard only trips if a NEW join() attempt started before this
          // queued signal was delivered (giving mRoom a fresh, non-null
          // value) — in that case, this stale joinFailed() belongs to an
          // attempt the caller has already moved on from, and firing it
          // would misattribute it to whatever join is now in progress.
          if (mRoom) {
            return;
          }
          emit joinFailed(QStringLiteral("Failed to connect to the video server."));
        },
        Qt::QueuedConnection);
    return;
  }

  publishTracks();

  // Deferred for the same reason as the joinFailed() emission above. Guarded
  // the opposite way: this join succeeded (mRoom is non-null right now), so
  // if mRoom is null by the time this runs, leave() (or a subsequent failed
  // join()) has already ended the session this joined() would refer to.
  //
  // Posted before the already-present-participant check below (not after):
  // onParticipantConnected() runs on a LiveKit-internal thread and queues
  // its own event the instant a participant joins, which can race with
  // this GUI-thread code at any point after connect() returns. Posting
  // joined() first keeps its queue position as early as possible, so a
  // real onParticipantConnected() event that got queued during
  // publishTracks() (and would otherwise be silently dropped, since
  // Joining has no transition for it) is much less likely to be ordered
  // ahead of joined() — narrowing, though not eliminating, that window.
  QMetaObject::invokeMethod(
      this,
      [this]() {
        if (!mRoom) {
          return;
        }
        emit joined();
      },
      Qt::QueuedConnection);

  // onParticipantConnected() only fires for participants who join AFTER
  // this connect() call — if the other party was already in the room (the
  // common case for a scheduled call both sides join around the same
  // time), that event never arrives and WaitingForClient would wait
  // forever. Check for an already-present participant here instead.
  if (!mRoom->remoteParticipants().empty()) {
    // Queued after (not together with) joined() above so it is delivered
    // strictly later: both are posted to the same object's event queue in
    // FIFO order, so VideoSession is guaranteed to process
    // Joining->WaitingForClient before WaitingForClient->Connected.
    QMetaObject::invokeMethod(
        this,
        [this]() {
          if (!mRoom) {
            return;
          }
          emit remoteParticipantConnected();
        },
        Qt::QueuedConnection);
  }
}

QWidget *LiveKitVideoProvider::remoteVideoWidget() { return mRemoteVideo.data(); }

QWidget *LiveKitVideoProvider::localVideoWidget() { return mLocalPreviewWidget.data(); }

void LiveKitVideoProvider::setMicrophoneEnabled(bool enabled) {
  mMicrophoneEnabled = enabled;
  if (mAudioTrack) {
    // mute()/unmute() are documented (local_audio_track.h) to throw
    // std::runtime_error on FFI failure. This is a local device/publish
    // problem, never network loss, so it is routed through mediaError() —
    // same rationale and same try/catch shape as publishTracks() below —
    // rather than left to escape uncaught out of this slot (CallPage's mute
    // button calls this directly).
    try {
      enabled ? mAudioTrack->unmute() : mAudioTrack->mute();
    } catch (const std::exception &e) {
      emit mediaError(QStringLiteral("Failed to %1 microphone: %2")
                          .arg(enabled ? QStringLiteral("unmute") : QStringLiteral("mute"))
                          .arg(e.what()));
    }
  }
}

void LiveKitVideoProvider::setCameraEnabled(bool enabled) {
  mCameraEnabled = enabled;
  if (mVideoTrack) {
    // See setMicrophoneEnabled() above: mute()/unmute() can throw
    // std::runtime_error on FFI failure (local_video_track.h), and that must
    // surface as mediaError(), not escape this slot uncaught.
    try {
      enabled ? mVideoTrack->unmute() : mVideoTrack->mute();
    } catch (const std::exception &e) {
      emit mediaError(QStringLiteral("Failed to %1 camera: %2")
                          .arg(enabled ? QStringLiteral("unmute") : QStringLiteral("mute"))
                          .arg(e.what()));
    }
  }
}

void LiveKitVideoProvider::switchCamera(const QCameraDevice &device) {
  // Same livekit::VideoSource the whole call publishes from — start()
  // only restarts the Qt-side QCamera/capture worker, so no republish is
  // needed.
  mVideoCapture->start(device);
}

void LiveKitVideoProvider::switchMicrophone(const QAudioDevice &device) {
  mAudioCapture->start(device);
}

void LiveKitVideoProvider::switchSpeaker(const QAudioDevice &device) {
  mSelectedSpeaker = device;
  if (mRemoteAudioTrack) {
    mRemoteAudio->detach();
    mRemoteAudio->attachTrack(mRemoteAudioTrack, device);
  }
}

void LiveKitVideoProvider::leave() {
  if (mRemoteVideo) {
    mRemoteVideo->detach();
  }
  mRemoteAudio->detach();
  mRemoteAudioTrack.reset();

  mVideoCapture->stop();
  mAudioCapture->stop();

  if (mRoom) {
    unpublishTracks();
    mRoom->setDelegate(nullptr);
    mRoom.reset();
  }
  // Always emitted, even if mRoom was already null (e.g. leave() called
  // while a join() attempt was still in flight, or called a second time):
  // VideoSession's Leaving state has exactly one way out, on this signal —
  // emitting it only when mRoom was non-null left Leaving stranded forever
  // whenever the room hadn't (or no longer) existed.
  emit left();
}

void LiveKitVideoProvider::publishTracks() {
  auto localParticipant = mRoom->localParticipant().lock();
  if (!localParticipant) {
    return;
  }

  try {
    mAudioTrack = livekit::LocalAudioTrack::createLocalAudioTrack("mic", mAudioCapture->audioSource());
    livekit::TrackPublishOptions audioOptions;
    audioOptions.source = livekit::TrackSource::SOURCE_MICROPHONE;
    audioOptions.dtx = false;
    audioOptions.simulcast = false;
    localParticipant->publishTrack(mAudioTrack, audioOptions);
    if (!mMicrophoneEnabled) {
      mAudioTrack->mute();
    }
  } catch (const std::exception &e) {
    emit mediaError(QStringLiteral("Failed to publish audio: %1").arg(e.what()));
  }

  try {
    mVideoTrack = livekit::LocalVideoTrack::createLocalVideoTrack("cam", mVideoCapture->videoSource());
    livekit::TrackPublishOptions videoOptions;
    videoOptions.source = livekit::TrackSource::SOURCE_CAMERA;
    videoOptions.dtx = false;
    videoOptions.simulcast = true;
    localParticipant->publishTrack(mVideoTrack, videoOptions);
    if (!mCameraEnabled) {
      mVideoTrack->mute();
    }
  } catch (const std::exception &e) {
    emit mediaError(QStringLiteral("Failed to publish video: %1").arg(e.what()));
  }
}

void LiveKitVideoProvider::unpublishTracks() {
  auto localParticipant = mRoom->localParticipant().lock();
  if (localParticipant) {
    if (mAudioTrack) {
      localParticipant->unpublishTrack(mAudioTrack->sid());
    }
    if (mVideoTrack) {
      localParticipant->unpublishTrack(mVideoTrack->sid());
    }
  }
  mAudioTrack.reset();
  mVideoTrack.reset();
}

void LiveKitVideoProvider::onTrackSubscribed(livekit::Room &, const livekit::TrackSubscribedEvent &event) {
  if (!event.track) {
    return;
  }
  const auto kind = event.track->kind();
  auto track = event.track;

  QMetaObject::invokeMethod(
      this,
      [this, track, kind]() {
        if (!mRoom) {
          return;
        }
        if (kind == livekit::TrackKind::KIND_VIDEO) {
          if (mRemoteVideo) {
            mRemoteVideo->attachTrack(track);
          }
        } else if (kind == livekit::TrackKind::KIND_AUDIO) {
          mRemoteAudioTrack = track;
          const auto device = mSelectedSpeaker ? mSelectedSpeaker : mDeviceManager->defaultSpeaker();
          if (device) {
            mRemoteAudio->attachTrack(track, *device);
          }
        }
      },
      Qt::QueuedConnection);
}

void LiveKitVideoProvider::onParticipantConnected(livekit::Room &,
                                                  const livekit::ParticipantConnectedEvent &) {
  QMetaObject::invokeMethod(
      this,
      [this]() {
        if (!mRoom) {
          return;
        }
        emit remoteParticipantConnected();
      },
      Qt::QueuedConnection);
}

void LiveKitVideoProvider::onParticipantDisconnected(livekit::Room &,
                                                     const livekit::ParticipantDisconnectedEvent &) {
  // Fires on ANY remote participant leaving, not "the last one" — correct
  // for this module's actual scope (one practitioner, one client, one
  // remote participant ever expected), same 1:1 assumption already made by
  // onTrackSubscribed()/RemoteVideoRenderer's single-track rendering.
  QMetaObject::invokeMethod(
      this,
      [this]() {
        if (!mRoom) {
          return;
        }
        emit remoteParticipantDisconnected();
      },
      Qt::QueuedConnection);
}

void LiveKitVideoProvider::onDisconnected(livekit::Room &, const livekit::DisconnectedEvent &event) {
  const auto reasonCode = static_cast<int>(event.reason);
  QMetaObject::invokeMethod(
      this,
      [this, reasonCode]() {
        // leave() clears the delegate before resetting mRoom, so a
        // self-initiated disconnect from leave() itself should not reach
        // this callback at all. This guard exists for the remaining race:
        // the LiveKit-internal thread can read a still-non-null delegate_
        // and start dispatching this event concurrently with leave()
        // running on the GUI thread; by the time this queued lambda
        // actually runs, mRoom may already be null.
        if (!mRoom) {
          return;
        }
        emit connectionLost(
            QStringLiteral("Room disconnected (reason code %1).").arg(reasonCode));
      },
      Qt::QueuedConnection);
}

void LiveKitVideoProvider::onReconnecting(livekit::Room &, const livekit::ReconnectingEvent &) {
  QMetaObject::invokeMethod(
      this,
      [this]() {
        if (!mRoom) {
          return;
        }
        emit reconnecting();
      },
      Qt::QueuedConnection);
}

void LiveKitVideoProvider::onReconnected(livekit::Room &, const livekit::ReconnectedEvent &) {
  QMetaObject::invokeMethod(
      this,
      [this]() {
        if (!mRoom) {
          return;
        }
        emit reconnected();
      },
      Qt::QueuedConnection);
}

} // namespace pcm::video
