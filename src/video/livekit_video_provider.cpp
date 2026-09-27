#include "livekit_video_provider.h"

#include "audio_capture_adapter.h"
#include "device_manager.h"
#include "remote_audio_player.h"
#include "remote_video_renderer.h"
#include "video_capture_adapter.h"

#include <QMetaObject>
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

  connect(mVideoCapture.get(), &VideoCaptureAdapter::captureFailed, this,
          &VideoProvider::connectionLost);
  connect(mAudioCapture.get(), &AudioCaptureAdapter::captureFailed, this,
          &VideoProvider::connectionLost);

  if (const auto camera = mDeviceManager->defaultCamera()) {
    mVideoCapture->start(*camera);
  }
  if (const auto microphone = mDeviceManager->defaultMicrophone()) {
    mAudioCapture->start(*microphone);
  }
}

LiveKitVideoProvider::~LiveKitVideoProvider() {
  leave();
  mVideoCapture->stop();
  mAudioCapture->stop();
  delete mRemoteVideo.data();
  // No explicit releaseLiveKitRuntime() call here: mRuntimeGuard's own
  // destructor handles it automatically, and — because it is declared
  // first in the header — runs last, after mVideoCapture/mAudioCapture/
  // mRoom/every track member above has already been destroyed by the
  // implicit member-destruction that follows this destructor body. Adding
  // a call here would double-release against the guard's own release.
}

void LiveKitVideoProvider::join(const QString &url, const QString &token) {
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
        this, [this]() { emit joinFailed(QStringLiteral("Failed to connect to the video server.")); },
        Qt::QueuedConnection);
    return;
  }

  publishTracks();
  // Deferred for the same reason as the joinFailed() emission above.
  QMetaObject::invokeMethod(this, [this]() { emit joined(); }, Qt::QueuedConnection);
}

void LiveKitVideoProvider::leave() {
  if (mRemoteVideo) {
    mRemoteVideo->detach();
  }
  mRemoteAudio->detach();
  mRemoteAudioTrack.reset();

  if (mRoom) {
    unpublishTracks();
    mRoom->setDelegate(nullptr);
    mRoom.reset();
    emit left();
  }
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
  } catch (const std::exception &e) {
    emit connectionLost(QStringLiteral("Failed to publish audio: %1").arg(e.what()));
  }

  try {
    mVideoTrack = livekit::LocalVideoTrack::createLocalVideoTrack("cam", mVideoCapture->videoSource());
    livekit::TrackPublishOptions videoOptions;
    videoOptions.source = livekit::TrackSource::SOURCE_CAMERA;
    videoOptions.dtx = false;
    videoOptions.simulcast = true;
    localParticipant->publishTrack(mVideoTrack, videoOptions);
  } catch (const std::exception &e) {
    emit connectionLost(QStringLiteral("Failed to publish video: %1").arg(e.what()));
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
          const auto device = mDeviceManager->defaultSpeaker();
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

void LiveKitVideoProvider::onDisconnected(livekit::Room &, const livekit::DisconnectedEvent &event) {
  const auto reasonCode = static_cast<int>(event.reason);
  QMetaObject::invokeMethod(
      this,
      [this, reasonCode]() {
        // Also fires for our own leave()'s disconnect; by the time this
        // queued callback runs, leave() has already reset mRoom, so this
        // guard suppresses self-initiated disconnects and only reports
        // genuine unexpected drops.
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
