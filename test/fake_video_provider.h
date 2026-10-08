#pragma once

#include "video_provider.h"
#include <QHash>
#include <QPointer>
#include <memory>
#include <vector>

namespace pcm::video::test {

// Test-only VideoProvider double: join()/leave() are driven entirely by
// explicit method calls on this class rather than a real SDK/network, so
// VideoSession's state machine can be tested deterministically and without
// a display, camera, or microphone. Never part of the production library.
class FakeVideoProvider final : public VideoProvider {
public:
  using VideoProvider::VideoProvider;

  void join(const QString &url, const QString &token) override {
    mLastJoinUrl = url;
    mLastJoinToken = token;
    mSwitchCountsAtJoin = mSwitchCameraCallCount + mSwitchMicrophoneCallCount + mSwitchSpeakerCallCount;
    ++mJoinCallCount;
  }

  void leave() override {
    ++mLeaveCallCount;
  }

  VideoFrameSource *frameSource(const QString &id) override { return mSources.value(id); }

  void startScreenShare(const ScreenCaptureTarget &target) override {
    mLastScreenTarget = target;
    ++mStartScreenShareCallCount;
    mScreenSharing = true;
    emit screenSharingChanged(true);
  }
  void stopScreenShare() override {
    ++mStopScreenShareCallCount;
    mScreenSharing = false;
    emit screenSharingChanged(false);
  }
  [[nodiscard]] bool isScreenSharing() const override { return mScreenSharing; }
  VideoFrameSource *screenSource(const QString &id) override { return mScreens.value(id); }
  void simulateScreenSharing(const QString &id, bool sharing) {
    auto participant = participants()->participant(id);
    if (!participant) return;
    if (sharing && !mScreens.value(id)) mScreens.insert(id, new VideoFrameSource(this));
    if (!sharing) delete mScreens.take(id);
    participant->screenSharing = sharing;
    participants()->upsert(*participant);
  }

  void setMicrophoneEnabled(bool enabled) override {
    if (!mRejectMediaChanges)
      mMicrophoneEnabled = enabled;
    ++mSetMicrophoneEnabledCallCount;
  }
  void setCameraEnabled(bool enabled) override {
    if (!mRejectMediaChanges)
      mCameraEnabled = enabled;
    ++mSetCameraEnabledCallCount;
  }
  [[nodiscard]] bool isMicrophoneEnabled() const override { return mMicrophoneEnabled; }
  [[nodiscard]] bool isCameraEnabled() const override { return mCameraEnabled; }

  void switchCamera(const QCameraDevice &device) override {
    mLastSwitchedCamera = device;
    ++mSwitchCameraCallCount;
  }
  void switchMicrophone(const QAudioDevice &device) override {
    mLastSwitchedMicrophone = device;
    ++mSwitchMicrophoneCallCount;
    simulateMicrophoneChanged(device);
  }
  [[nodiscard]] QAudioDevice selectedMicrophone() const override { return mSelectedMicrophone; }
  void simulateMicrophoneChanged(const QAudioDevice &device) {
    mSelectedMicrophone = device;
    emit microphoneChanged(device);
  }
  void switchSpeaker(const QAudioDevice &device) override {
    mLastSwitchedSpeaker = device;
    ++mSwitchSpeakerCallCount;
  }

  // Test-driving methods: call these to simulate the real provider emitting
  // its outcome signals asynchronously, exactly as LiveKitVideoProvider
  // would once its own SDK callbacks fire.
  void setAudioSink(std::shared_ptr<AudioSink> sink) override { mSink = std::move(sink); }
  [[nodiscard]] std::shared_ptr<AudioSink> audioSink() const { return mSink; }
  void simulateAudio(const QString &id, const std::vector<int16_t> &samples, int rate = 48000) {
    if (mSink)
      mSink->onAudio(id, samples.data(), samples.size(), rate);
  }
  [[nodiscard]] qint64 callElapsedMs() const override { return mCallElapsedMs; }
  qint64 mCallElapsedMs{0};
  void simulateJoined() { emit joined(); }
  void simulateJoinFailed(const QString &reason) { emit joinFailed(reason); }
  void simulateLeft() { participants()->clear(); qDeleteAll(mSources); mSources.clear(); qDeleteAll(mScreens); mScreens.clear(); emit left(); }
  void simulateParticipantJoined(const Participant &participant) {
    const bool exists = participants()->participant(participant.id).has_value();
    if (!mSources.value(participant.id))
      mSources.insert(participant.id, new VideoFrameSource(this));
    participants()->upsert(participant);
    if (!exists)
      emit participantJoined(participant.id);
  }
  void simulateParticipantLeft(const QString &id) {
    if (!participants()->participant(id))
      return;
    participants()->remove(id);
    delete mSources.take(id);
    delete mScreens.take(id);
    emit participantLeft(id);
  }
  void simulateReconnecting() { emit reconnecting(); }
  void simulateReconnected() { emit reconnected(); }
  void simulateConnectionLost(const QString &reason) { emit connectionLost(reason); }
  void simulateMediaError(const QString &reason) { emit mediaError(reason); }

  std::shared_ptr<AudioSink> mSink;
  QString mLastJoinUrl;
  QString mLastJoinToken;
  int mJoinCallCount{0};
  // Total switchCamera/Microphone/Speaker calls that had already happened when join() ran.
  int mSwitchCountsAtJoin{0};
  int mLeaveCallCount{0};
  QHash<QString, QPointer<VideoFrameSource>> mSources;
  QHash<QString, QPointer<VideoFrameSource>> mScreens;
  bool mRejectMediaChanges{false};
  bool mScreenSharing{false};
  int mStartScreenShareCallCount{0};
  int mStopScreenShareCallCount{0};
  ScreenCaptureTarget mLastScreenTarget;
  bool mMicrophoneEnabled{true};
  bool mCameraEnabled{true};
  int mSetMicrophoneEnabledCallCount{0};
  int mSetCameraEnabledCallCount{0};
  int mSwitchCameraCallCount{0};
  int mSwitchMicrophoneCallCount{0};
  int mSwitchSpeakerCallCount{0};
  QCameraDevice mLastSwitchedCamera;
  QAudioDevice mLastSwitchedMicrophone;
  QAudioDevice mSelectedMicrophone;
  QAudioDevice mLastSwitchedSpeaker;
};

} // namespace pcm::video::test
