#pragma once

#include "video_provider.h"

#include <QPointer>
// The brief's original spike code included only <livekit/room.h>, but that
// header merely forward-declares LocalParticipant and doesn't declare
// LocalAudioTrack/LocalVideoTrack at all, nor livekit::initialize()/
// shutdown() (those live only in the umbrella header). <livekit/livekit.h>
// is the vendored SDK's public umbrella header (see
// build/_deps/livekit-sdk/*/include/livekit/livekit.h) and pulls in room.h
// plus local_audio_track.h/local_video_track.h/local_participant.h and the
// initialize()/shutdown() declarations this class needs.
#include <livekit/livekit.h>
#include <memory>

namespace pcm::video {

class VideoCaptureAdapter;
class AudioCaptureAdapter;
class RemoteVideoRenderer;
class RemoteAudioPlayer;
class DeviceManager;

// Production VideoProvider implementation: connects to a real LiveKit
// server, captures the default camera/microphone, publishes local tracks,
// and renders/plays the first subscribed remote video/audio track. Ported
// from spike/77-livekit-cpp-spike, with every stderr-only error path turned
// into a signal.
class LiveKitVideoProvider final : public VideoProvider, private livekit::RoomDelegate {
  Q_OBJECT
public:
  explicit LiveKitVideoProvider(QObject *parent = nullptr);
  ~LiveKitVideoProvider() override;

  void join(const QString &url, const QString &token) override;
  void leave() override;

private:
  // livekit::RoomDelegate overrides — invoked on a LiveKit-internal thread;
  // every override marshals to the GUI thread via QMetaObject::invokeMethod
  // and guards its body with `if (!mRoom) return;` since leave() may run
  // before a queued callback executes.
  void onTrackSubscribed(livekit::Room &room, const livekit::TrackSubscribedEvent &event) override;
  void onParticipantConnected(livekit::Room &room,
                              const livekit::ParticipantConnectedEvent &event) override;
  // Wired so VideoSession's Connected<->Reconnecting state graph (Task 8)
  // actually has something driving it from real network events, not just
  // local device-capture failures. onDisconnected fires both for genuine
  // drops and for our own leave()'s disconnect; the `if (!mRoom) return;`
  // guard suppresses the latter, since leave() resets mRoom before this
  // queued callback can run.
  void onDisconnected(livekit::Room &room, const livekit::DisconnectedEvent &event) override;
  void onReconnecting(livekit::Room &room, const livekit::ReconnectingEvent &event) override;
  void onReconnected(livekit::Room &room, const livekit::ReconnectedEvent &event) override;

  void publishTracks();
  void unpublishTracks();

  // Reference-counts livekit::initialize()/shutdown() (see the anonymous
  // namespace in the .cpp). MUST be the first member declared in this
  // class: C++ constructs members in declaration order and destroys them
  // in reverse — declaring it first guarantees livekit::initialize() runs
  // before mVideoCapture/mAudioCapture below (whose constructors make
  // LiveKit FFI calls: VideoCaptureAdapter constructs a livekit::VideoSource,
  // AudioCaptureAdapter a livekit::AudioSource) and that livekit::shutdown()
  // runs only after every other member — including those two — has already
  // been destroyed. Do not reorder this declaration relative to the members
  // below it, and do not add it to this class's constructor's member-init
  // list (its default constructor already runs at the right time by virtue
  // of declaration order alone).
  struct LiveKitRuntimeGuard {
    LiveKitRuntimeGuard();
    ~LiveKitRuntimeGuard();
  };
  LiveKitRuntimeGuard mRuntimeGuard;

  std::unique_ptr<DeviceManager> mDeviceManager;
  std::unique_ptr<VideoCaptureAdapter> mVideoCapture;
  std::unique_ptr<AudioCaptureAdapter> mAudioCapture;
  QPointer<RemoteVideoRenderer> mRemoteVideo;
  std::unique_ptr<RemoteAudioPlayer> mRemoteAudio;

  std::unique_ptr<livekit::Room> mRoom;
  std::shared_ptr<livekit::LocalAudioTrack> mAudioTrack;
  std::shared_ptr<livekit::LocalVideoTrack> mVideoTrack;
  std::shared_ptr<livekit::Track> mRemoteAudioTrack;
};

} // namespace pcm::video
