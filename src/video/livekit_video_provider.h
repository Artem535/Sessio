#pragma once

#include "video_provider.h"

#include <livekit/livekit.h>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <QElapsedTimer>

namespace pcm::video {

class VideoCaptureAdapter;
class ScreenCaptureAdapter;
class AudioCaptureAdapter;
class LiveKitVideoFrameSource;
class RemoteAudioPlayer;
class DeviceManager;

// Copied SDK callback values; only track shared ownership crosses threads.
struct ParticipantSnapshot {
  Participant value;
  QString sid;
};
struct TrackSnapshot {
  QString id;
  QString participantSid;
  QString sid;
  livekit::TrackKind kind;
  bool muted;
  std::shared_ptr<livekit::Track> track;
  bool screen{false};
};

class LiveKitVideoProvider final : public VideoProvider {
  Q_OBJECT
public:
  explicit LiveKitVideoProvider(QObject *parent = nullptr);
  ~LiveKitVideoProvider() override;

  void join(const QString &url, const QString &token) override;
  void leave() override;
  VideoFrameSource *frameSource(const QString &id) override;
  VideoFrameSource *screenSource(const QString &id) override;
  void setMicrophoneEnabled(bool enabled) override;
  void setCameraEnabled(bool enabled) override;
  [[nodiscard]] bool isMicrophoneEnabled() const override { return mMicrophoneEnabled; }
  [[nodiscard]] bool isCameraEnabled() const override { return mCameraEnabled; }
  void startScreenShare(const ScreenCaptureTarget &target) override;
  void stopScreenShare() override;
  [[nodiscard]] bool isScreenSharing() const override { return mScreenTrack != nullptr; }
  void switchCamera(const QCameraDevice &device) override;
  void switchMicrophone(const QAudioDevice &device) override;
  void switchSpeaker(const QAudioDevice &device) override;
  void setAudioSink(std::shared_ptr<AudioSink> sink) override;
  [[nodiscard]] qint64 callElapsedMs() const override;
  [[nodiscard]] QAudioDevice selectedMicrophone() const override;

private:
  friend struct LiveKitVideoProviderTestAccess;
  class CallbackDelegate;
  struct ParticipantMedia {
    QString sid;
    std::unique_ptr<LiveKitVideoFrameSource> video;
    std::unique_ptr<LiveKitVideoFrameSource> screen;
    QString screenSid;
    QString lastScreenSid;
    std::set<QString> retiredScreenSids;
    std::unique_ptr<RemoteAudioPlayer> audio;
    QString videoSid;
    QString audioSid;
    // Retained through unsubscribe so a different successor retires this SID;
    // attached SID above can be empty while this publication can resubscribe.
    QString lastVideoSid;
    QString lastAudioSid;
    std::set<QString> retiredVideoSids;
    std::set<QString> retiredAudioSids;
    std::shared_ptr<livekit::Track> audioTrack;
  };

  void queueCallback(uint64_t generation, std::function<void(LiveKitVideoProvider &)> callback);
  void applyParticipant(const ParticipantSnapshot &snapshot, bool allowInsert);
  void applyDeparture(const QString &id, const QString &sid);
  void applySubscribed(const TrackSnapshot &snapshot);
  void applyUnsubscribed(const TrackSnapshot &snapshot);
  void applyMuted(const TrackSnapshot &snapshot, bool muted);
  void updateLocalState();
  void publishTracks();
  void unpublishTracks();
  void teardown();
  void snapshotParticipants();

  // First constructed, last destroyed: every stream/track/capture dies before
  // the final SDK shutdown. Never move this below SDK-holding members.
  struct LiveKitRuntimeGuard {
    LiveKitRuntimeGuard();
    ~LiveKitRuntimeGuard();
  };
  LiveKitRuntimeGuard mRuntimeGuard;

  std::unique_ptr<DeviceManager> mDeviceManager;
  std::unique_ptr<VideoCaptureAdapter> mVideoCapture;
  std::unique_ptr<ScreenCaptureAdapter> mScreenCapture;
  std::unique_ptr<AudioCaptureAdapter> mAudioCapture;
  std::map<QString, std::unique_ptr<ParticipantMedia>> mMedia;
  std::set<std::pair<QString, QString>> mDeparted;
  // Shared with the taps so their lambdas stay valid independent of provider teardown.
  std::shared_ptr<AudioSinkSlot> mSinkSlot{std::make_shared<AudioSinkSlot>()};
  QString mLocalIdentity;
  bool mMicrophoneEnabled{true};
  bool mCameraEnabled{true};
  std::optional<QAudioDevice> mSelectedSpeaker;
  // Chosen before joining (device-check screen); join() opens these instead of the defaults.
  std::optional<QCameraDevice> mSelectedCamera;
  std::optional<QAudioDevice> mSelectedMicrophone;
  std::optional<QAudioDevice> mPendingMicrophone;
  bool mMicrophoneSwitchQueued{false};
  QElapsedTimer mCallClock;
  uint64_t mGeneration{0};
  std::mutex mCallbackMutex;
  uint64_t mNextCallback{0};
  std::map<uint64_t, std::function<void(LiveKitVideoProvider &)>> mPendingCallbacks;

  // Room is destroyed before its delegate. Delegate invalidation is synchronized
  // with callback dispatch; queued work retains its original generation.
  std::unique_ptr<CallbackDelegate> mDelegate;
  std::unique_ptr<livekit::Room> mRoom;
  std::shared_ptr<livekit::LocalAudioTrack> mAudioTrack;
  std::shared_ptr<livekit::LocalVideoTrack> mVideoTrack;
  std::shared_ptr<livekit::LocalVideoTrack> mScreenTrack;
};

} // namespace pcm::video
