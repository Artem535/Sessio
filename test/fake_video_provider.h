#pragma once

#include "video_provider.h"
#include <QHash>
#include <QPointer>

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
    ++mJoinCallCount;
  }

  void leave() override {
    ++mLeaveCallCount;
  }

  VideoFrameSource *frameSource(const QString &id) override { return mSources.value(id); }

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
  }
  void switchSpeaker(const QAudioDevice &device) override {
    mLastSwitchedSpeaker = device;
    ++mSwitchSpeakerCallCount;
  }

  // Test-driving methods: call these to simulate the real provider emitting
  // its outcome signals asynchronously, exactly as LiveKitVideoProvider
  // would once its own SDK callbacks fire.
  void simulateJoined() { emit joined(); }
  void simulateJoinFailed(const QString &reason) { emit joinFailed(reason); }
  void simulateLeft() { participants()->clear(); qDeleteAll(mSources); mSources.clear(); emit left(); }
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
    emit participantLeft(id);
  }
  void simulateReconnecting() { emit reconnecting(); }
  void simulateReconnected() { emit reconnected(); }
  void simulateConnectionLost(const QString &reason) { emit connectionLost(reason); }
  void simulateMediaError(const QString &reason) { emit mediaError(reason); }

  QString mLastJoinUrl;
  QString mLastJoinToken;
  int mJoinCallCount{0};
  int mLeaveCallCount{0};
  QHash<QString, QPointer<VideoFrameSource>> mSources;
  bool mRejectMediaChanges{false};
  bool mMicrophoneEnabled{true};
  bool mCameraEnabled{true};
  int mSetMicrophoneEnabledCallCount{0};
  int mSetCameraEnabledCallCount{0};
  int mSwitchCameraCallCount{0};
  int mSwitchMicrophoneCallCount{0};
  int mSwitchSpeakerCallCount{0};
  QCameraDevice mLastSwitchedCamera;
  QAudioDevice mLastSwitchedMicrophone;
  QAudioDevice mLastSwitchedSpeaker;
};

} // namespace pcm::video::test
