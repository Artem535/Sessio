#include "livekit_video_provider.h"

#include "audio_capture_adapter.h"
#include "device_manager.h"
#include "livekit_video_frame_source.h"
#include "remote_audio_player.h"
#include "video_capture_adapter.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QVideoFrame>
#include <QVideoSink>
#include <atomic>
#include <chrono>
#include <mutex>

namespace pcm::video {
namespace {
std::atomic<int> gLiveKitRefCount{0};

QString metadataRole(const QString &metadata) {
  return QJsonDocument::fromJson(metadata.toUtf8()).object().value("role").toString();
}

ParticipantSnapshot copyParticipant(const livekit::Participant &participant) {
  ParticipantSnapshot result;
  result.sid = QString::fromStdString(participant.sid());
  result.value.id = QString::fromStdString(participant.identity());
  result.value.displayName = QString::fromStdString(participant.name());
  result.value.role = metadataRole(QString::fromStdString(participant.metadata()));
  result.value.microphoneEnabled = false;
  result.value.cameraEnabled = false;
  if (const auto *remote = dynamic_cast<const livekit::RemoteParticipant *>(&participant)) {
    for (const auto &[sid, publication] : remote->trackPublications()) {
      if (publication->kind() == livekit::TrackKind::KIND_VIDEO && !publication->muted())
        result.value.cameraEnabled = true;
      if (publication->kind() == livekit::TrackKind::KIND_AUDIO && !publication->muted())
        result.value.microphoneEnabled = true;
    }
  } else {
    result.value.isLocal = true;
  }
  return result;
}

TrackSnapshot copyTrack(const livekit::Participant &participant,
                        const livekit::TrackPublication &publication,
                        std::shared_ptr<livekit::Track> track = {}) {
  return {QString::fromStdString(participant.identity()),
          QString::fromStdString(participant.sid()), QString::fromStdString(publication.sid()),
          publication.kind(), publication.muted(), std::move(track)};
}
} // namespace

// Each room owns a different bridge with an immutable generation. The mutex
// protects target lifetime through copying + invokeMethod(), without touching
// QObject/QPointer state on SDK threads. Invalidation waits for dispatch and the
// bridge stays alive until Room destruction has removed/joined SDK listeners.
class LiveKitVideoProvider::CallbackDelegate final : public livekit::RoomDelegate {
public:
  CallbackDelegate(LiveKitVideoProvider &target, livekit::Room &room, uint64_t generation)
      : mTarget(&target), mRoom(&room), mGeneration(generation) {}
  void invalidate() {
    std::lock_guard lock(mMutex);
    mTarget = nullptr;
  }

  void onParticipantConnected(livekit::Room &room,
                              const livekit::ParticipantConnectedEvent &event) override {
    if (!event.participant) return;
    dispatch(room, [&] {
      const auto snapshot = copyParticipant(*event.participant);
      return [snapshot](LiveKitVideoProvider &target) { target.applyParticipant(snapshot, true); };
    });
  }

  void onParticipantDisconnected(livekit::Room &room,
                                 const livekit::ParticipantDisconnectedEvent &event) override {
    if (!event.participant) return;
    dispatch(room, [&] {
      const auto id = QString::fromStdString(event.participant->identity());
      const auto sid = QString::fromStdString(event.participant->sid());
      return [id, sid](LiveKitVideoProvider &target) { target.applyDeparture(id, sid); };
    });
  }

  void onTrackSubscribed(livekit::Room &room, const livekit::TrackSubscribedEvent &event) override {
    if (!event.participant || !event.publication || !event.track) return;
    dispatch(room, [&] {
      const auto participant = copyParticipant(*event.participant);
      const auto track = copyTrack(*event.participant, *event.publication, event.track);
      return [participant, track](LiveKitVideoProvider &target) {
        target.applyParticipant(participant, false);
        target.applySubscribed(track);
      };
    });
  }

  void onTrackUnsubscribed(livekit::Room &room, const livekit::TrackUnsubscribedEvent &event) override {
    if (!event.participant || !event.publication) return;
    dispatch(room, [&] {
      const auto track = copyTrack(*event.participant, *event.publication);
      return [track](LiveKitVideoProvider &target) { target.applyUnsubscribed(track); };
    });
  }

  void onTrackUnpublished(livekit::Room &room, const livekit::TrackUnpublishedEvent &event) override {
    if (!event.participant || !event.publication) return;
    dispatch(room, [&] {
      const auto track = copyTrack(*event.participant, *event.publication);
      return [track](LiveKitVideoProvider &target) { target.applyUnsubscribed(track); };
    });
  }

  void onTrackMuted(livekit::Room &room, const livekit::TrackMutedEvent &event) override {
    if (!event.participant || !event.publication) return;
    dispatch(room, [&] {
      const auto track = copyTrack(*event.participant, *event.publication);
      return [track](LiveKitVideoProvider &target) { target.applyMuted(track, true); };
    });
  }

  void onTrackUnmuted(livekit::Room &room, const livekit::TrackUnmutedEvent &event) override {
    if (!event.participant || !event.publication) return;
    dispatch(room, [&] {
      const auto track = copyTrack(*event.participant, *event.publication);
      return [track](LiveKitVideoProvider &target) { target.applyMuted(track, false); };
    });
  }

  void onParticipantMetadataChanged(livekit::Room &room,
                                   const livekit::ParticipantMetadataChangedEvent &event) override {
    updateParticipant(room, event.participant);
  }
  void onParticipantNameChanged(livekit::Room &room,
                               const livekit::ParticipantNameChangedEvent &event) override {
    updateParticipant(room, event.participant);
  }
  void onParticipantsUpdated(livekit::Room &room,
                             const livekit::ParticipantsUpdatedEvent &event) override {
    for (const auto *participant : event.participants) updateParticipant(room, participant);
  }

  void onDisconnected(livekit::Room &room, const livekit::DisconnectedEvent &event) override {
    dispatch(room, [&] {
      const int reason = static_cast<int>(event.reason);
      return [reason](LiveKitVideoProvider &target) {
        target.teardown();
        emit target.connectionLost(QStringLiteral("Room disconnected (reason code %1).").arg(reason));
      };
    });
  }
  void onReconnecting(livekit::Room &room, const livekit::ReconnectingEvent &) override {
    dispatch(room, [] {
      return [](LiveKitVideoProvider &target) { emit target.reconnecting(); };
    });
  }
  void onReconnected(livekit::Room &room, const livekit::ReconnectedEvent &) override {
    dispatch(room, [] {
      return [](LiveKitVideoProvider &target) {
        target.snapshotParticipants();
        emit target.reconnected();
      };
    });
  }

private:
  template <typename Prepare>
  void dispatch(livekit::Room &room, Prepare prepare) {
    std::lock_guard lock(mMutex);
    if (mTarget && &room == mRoom) {
      mTarget->queueCallback(mGeneration, prepare());
    }
  }

  void updateParticipant(livekit::Room &room, const livekit::Participant *participant) {
    if (!participant) return;
    dispatch(room, [&] {
      const auto snapshot = copyParticipant(*participant);
      return [snapshot](LiveKitVideoProvider &target) { target.applyParticipant(snapshot, false); };
    });
  }
  std::mutex mMutex;
  LiveKitVideoProvider *mTarget;
  const livekit::Room *mRoom;
  const uint64_t mGeneration;
};

LiveKitVideoProvider::LiveKitRuntimeGuard::LiveKitRuntimeGuard() {
  if (gLiveKitRefCount.fetch_add(1) == 0) livekit::initialize(livekit::LogLevel::Warn);
}
LiveKitVideoProvider::LiveKitRuntimeGuard::~LiveKitRuntimeGuard() {
  if (gLiveKitRefCount.fetch_sub(1) == 1) livekit::shutdown();
}

LiveKitVideoProvider::LiveKitVideoProvider(QObject *parent)
    : VideoProvider(parent), mDeviceManager(std::make_unique<DeviceManager>()),
      mVideoCapture(std::make_unique<VideoCaptureAdapter>()),
      mAudioCapture(std::make_unique<AudioCaptureAdapter>()) {
  connect(mVideoCapture.get(), &VideoCaptureAdapter::captureFailed, this, &VideoProvider::mediaError);
  connect(mAudioCapture.get(), &AudioCaptureAdapter::captureFailed, this, &VideoProvider::mediaError);
  connect(mVideoCapture->previewSink(), &QVideoSink::videoFrameChanged, this,
          [this](const QVideoFrame &frame) {
    if (!mCameraEnabled) return;
    if (const auto source = frameSource(mLocalIdentity); source && frame.isValid())
      source->submitFrame(frame.toImage());
  });
}

LiveKitVideoProvider::~LiveKitVideoProvider() { teardown(); }

void LiveKitVideoProvider::queueCallback(uint64_t generation,
                                        std::function<void(LiveKitVideoProvider &)> callback) {
  uint64_t sequence;
  {
    std::lock_guard lock(mCallbackMutex);
    sequence = ++mNextCallback;
    mPendingCallbacks.emplace(sequence, std::move(callback));
  }
  // Qt owns only integer keys. SDK payloads remain ours so teardown can release
  // every track before the runtime guard shuts down (before QObject destruction).
  QMetaObject::invokeMethod(this, [this, generation, sequence] {
    std::function<void(LiveKitVideoProvider &)> pending;
    {
      std::lock_guard lock(mCallbackMutex);
      const auto it = mPendingCallbacks.find(sequence);
      if (it == mPendingCallbacks.end()) return;
      pending = std::move(it->second);
      mPendingCallbacks.erase(it);
    }
    if (mRoom && generation == mGeneration) pending(*this);
  }, Qt::QueuedConnection);
}

VideoFrameSource *LiveKitVideoProvider::frameSource(const QString &id) {
  const auto it = mMedia.find(id);
  return it == mMedia.end() ? nullptr : it->second->video.get();
}

void LiveKitVideoProvider::join(const QString &url, const QString &token) {
  teardown();
  const auto generation = mGeneration;
  if (const auto camera = mDeviceManager->defaultCamera()) mVideoCapture->start(*camera);
  if (const auto microphone = mDeviceManager->defaultMicrophone()) mAudioCapture->start(*microphone);

  mRoom = std::make_unique<livekit::Room>();
  mDelegate = std::make_unique<CallbackDelegate>(*this, *mRoom, generation);
  mRoom->setDelegate(mDelegate.get());
  livekit::RoomOptions options;
  options.auto_subscribe = true;
  options.dynacast = false;
  options.join_retries = 0;
  options.connect_timeout = std::chrono::seconds(5);
  bool connected = false;
  try {
    connected = mRoom->connect(url.toStdString(), token.toStdString(), options);
  } catch (const std::exception &) {
    // URLs/tokens and SDK exception text can contain secrets: use a fixed error.
  }
  if (!connected) {
    teardown();
    const auto failedGeneration = mGeneration;
    QMetaObject::invokeMethod(this, [this, failedGeneration] {
      if (mGeneration == failedGeneration && !mRoom)
        emit joinFailed(QStringLiteral("Failed to connect to the video server."));
    }, Qt::QueuedConnection);
    return;
  }

  snapshotParticipants();
  // Token metadata is display-only. Older tokens simply yield an empty role.
  const auto payload = QJsonDocument::fromJson(QByteArray::fromBase64(
      token.section('.', 1, 1).toLatin1(), QByteArray::Base64UrlEncoding)).object();
  if (auto local = participants()->participant(mLocalIdentity)) {
    local->role = metadataRole(payload.value("metadata").toString());
    participants()->upsert(*local);
  }
  publishTracks();
  queueCallback(generation, [](LiveKitVideoProvider &target) { emit target.joined(); });
}

void LiveKitVideoProvider::snapshotParticipants() {
  if (!mRoom) return;
  // The SDK synchronizes room snapshots but its participant/name/metadata and
  // publication getters are plain. Copy only immutable identity/SID here;
  // owner-event-thread callbacks safely enrich display/media fields afterwards.
  if (const auto local = mRoom->localParticipant().lock()) {
    mLocalIdentity = QString::fromStdString(local->identity());
    if (!participants()->participant(mLocalIdentity)) {
      applyParticipant({{mLocalIdentity, {}, {}, true, mMicrophoneEnabled, mCameraEnabled},
                        QString::fromStdString(local->sid())}, true);
    }
  }
  std::set<QString> present;
  for (const auto &weak : mRoom->remoteParticipants()) {
    if (const auto remote = weak.lock()) {
      const auto id = QString::fromStdString(remote->identity());
      present.insert(id);
      const auto sid = QString::fromStdString(remote->sid());
      const auto it = mMedia.find(id);
      if (it == mMedia.end() || it->second->sid != sid)
        applyParticipant({{id, {}, {}, false, false, false}, sid}, true);
    }
  }
  std::vector<std::pair<QString, QString>> absent;
  for (const auto &[id, media] : mMedia) {
    if (id != mLocalIdentity && !present.contains(id)) absent.emplace_back(id, media->sid);
  }
  for (const auto &[id, sid] : absent) applyDeparture(id, sid);
}

void LiveKitVideoProvider::applyParticipant(const ParticipantSnapshot &snapshot, bool allowInsert) {
  const auto &id = snapshot.value.id;
  if (id.isEmpty() || mDeparted.contains({id, snapshot.sid})) return;
  auto it = mMedia.find(id);
  const bool newIdentity = it == mMedia.end();
  const bool sameParticipant = !newIdentity && it->second->sid == snapshot.sid;
  if (newIdentity && !allowInsert) return;
  if (!newIdentity && it->second->sid != snapshot.sid) {
    if (!allowInsert) return;
    mDeparted.insert({id, it->second->sid});
    it->second->video->detach();
    it->second->audio->detach();
    it->second->audioTrack.reset();
    it->second->videoSid.clear();
    it->second->audioSid.clear();
    it->second->lastVideoSid.clear();
    it->second->lastAudioSid.clear();
    it->second->retiredVideoSids.clear();
    it->second->retiredAudioSids.clear();
    it->second->sid = snapshot.sid;
  }
  if (newIdentity) {
    auto media = std::make_unique<ParticipantMedia>();
    media->sid = snapshot.sid;
    media->video = std::make_unique<LiveKitVideoFrameSource>();
    media->audio = std::make_unique<RemoteAudioPlayer>();
    connect(media->audio.get(), &RemoteAudioPlayer::playbackFailed, this, &VideoProvider::mediaError);
    mMedia.emplace(id, std::move(media)); // visible to rowsInserted observers
  }
  auto value = snapshot.value;
  if (sameParticipant) {
    // Display updates and duplicate connected events do not undo an unsubscribe
    // or mute; effective track state is updated by its own SID-guarded handler.
    if (const auto current = participants()->participant(id)) {
      value.microphoneEnabled = current->microphoneEnabled;
      value.cameraEnabled = current->cameraEnabled;
      value.isLocal = current->isLocal;
    }
  }
  if (value.isLocal) {
    value.microphoneEnabled = mMicrophoneEnabled;
    value.cameraEnabled = mCameraEnabled;
  }
  participants()->upsert(value);
  if (newIdentity) emit participantJoined(id);
}

void LiveKitVideoProvider::applyDeparture(const QString &id, const QString &sid) {
  const auto it = mMedia.find(id);
  if (it == mMedia.end() || it->second->sid != sid) return;
  mDeparted.insert({id, sid});
  participants()->remove(id);
  mMedia.erase(it); // source/player destructors close blocked reads before join
  emit participantLeft(id);
}

void LiveKitVideoProvider::applySubscribed(const TrackSnapshot &snapshot) {
  const auto it = mMedia.find(snapshot.id);
  auto participant = participants()->participant(snapshot.id);
  if (it == mMedia.end() || !participant || it->second->sid != snapshot.participantSid) return;
  auto &media = *it->second;
  try {
    if (snapshot.kind == livekit::TrackKind::KIND_VIDEO) {
      if (media.retiredVideoSids.contains(snapshot.sid)) return;
      if (media.videoSid == snapshot.sid) return;
      if (!media.lastVideoSid.isEmpty() && media.lastVideoSid != snapshot.sid)
        media.retiredVideoSids.insert(media.lastVideoSid);
      media.video->attachTrack(snapshot.track);
      media.videoSid = snapshot.sid;
      media.lastVideoSid = snapshot.sid;
      participant->cameraEnabled = !snapshot.muted;
    } else if (snapshot.kind == livekit::TrackKind::KIND_AUDIO) {
      if (media.retiredAudioSids.contains(snapshot.sid)) return;
      if (media.audioSid == snapshot.sid) return;
      if (!media.lastAudioSid.isEmpty() && media.lastAudioSid != snapshot.sid)
        media.retiredAudioSids.insert(media.lastAudioSid);
      media.audio->detach();
      media.audioSid = snapshot.sid;
      media.lastAudioSid = snapshot.sid;
      media.audioTrack = snapshot.track;
      const auto device = mSelectedSpeaker ? mSelectedSpeaker : mDeviceManager->defaultSpeaker();
      if (device) media.audio->attachTrack(snapshot.track, *device);
      participant->microphoneEnabled = !snapshot.muted;
    }
    participants()->upsert(*participant);
  } catch (const std::exception &) {
    emit mediaError(QStringLiteral("Failed to attach remote media."));
  }
}

void LiveKitVideoProvider::applyUnsubscribed(const TrackSnapshot &snapshot) {
  const auto it = mMedia.find(snapshot.id);
  auto participant = participants()->participant(snapshot.id);
  if (it == mMedia.end() || !participant || it->second->sid != snapshot.participantSid) return;
  auto &media = *it->second;
  if (snapshot.kind == livekit::TrackKind::KIND_VIDEO && media.videoSid == snapshot.sid) {
    media.video->detach();
    media.videoSid.clear();
    participant->cameraEnabled = false;
  } else if (snapshot.kind == livekit::TrackKind::KIND_AUDIO && media.audioSid == snapshot.sid) {
    media.audio->detach();
    media.audioTrack.reset();
    media.audioSid.clear();
    participant->microphoneEnabled = false;
  } else return;
  participants()->upsert(*participant);
}

void LiveKitVideoProvider::applyMuted(const TrackSnapshot &snapshot, bool muted) {
  const auto it = mMedia.find(snapshot.id);
  auto participant = participants()->participant(snapshot.id);
  if (it == mMedia.end() || !participant || it->second->sid != snapshot.participantSid) return;
  auto &media = *it->second;
  if (snapshot.kind == livekit::TrackKind::KIND_VIDEO && media.videoSid == snapshot.sid) {
    participant->cameraEnabled = !muted;
    if (muted) media.video->clear();
  } else if (snapshot.kind == livekit::TrackKind::KIND_AUDIO && media.audioSid == snapshot.sid) {
    participant->microphoneEnabled = !muted;
  } else return;
  participants()->upsert(*participant);
}

void LiveKitVideoProvider::updateLocalState() {
  if (auto local = participants()->participant(mLocalIdentity)) {
    local->microphoneEnabled = mMicrophoneEnabled;
    local->cameraEnabled = mCameraEnabled;
    if (!mCameraEnabled) {
      if (auto source = frameSource(mLocalIdentity)) source->clear();
    }
    participants()->upsert(*local);
  }
}

void LiveKitVideoProvider::setMicrophoneEnabled(bool enabled) {
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
      return;
    }
  }
  mMicrophoneEnabled = enabled;
  updateLocalState();
}

void LiveKitVideoProvider::setCameraEnabled(bool enabled) {
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
      return;
    }
  }
  mCameraEnabled = enabled;
  updateLocalState();
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
  for (auto &[id, media] : mMedia) {
    if (media->audioTrack) {
      try {
        media->audio->attachTrack(media->audioTrack, device);
      } catch (const std::exception &) {
        emit mediaError(QStringLiteral("Failed to switch audio output."));
      }
    }
  }
}


void LiveKitVideoProvider::teardown() {
  ++mGeneration;
  if (mDelegate) mDelegate->invalidate();
  {
    std::lock_guard lock(mCallbackMutex);
    mPendingCallbacks.clear();
  }
  if (mRoom) mRoom->setDelegate(nullptr);
  mVideoCapture->stop();
  mAudioCapture->stop();
  participants()->clear();
  mMedia.clear();
  mLocalIdentity.clear();
  mDeparted.clear();
  if (mRoom) {
    try {
      unpublishTracks();
    } catch (const std::exception &) {
      // Teardown must finish even when a disconnected room refuses unpublish.
    }
  }
  mAudioTrack.reset();
  mVideoTrack.reset();
  mRoom.reset();
  mDelegate.reset();
}

void LiveKitVideoProvider::leave() {
  teardown();
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


} // namespace pcm::video
