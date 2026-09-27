#pragma once

#include "video_provider.h"

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

  // Test-driving methods: call these to simulate the real provider emitting
  // its outcome signals asynchronously, exactly as LiveKitVideoProvider
  // would once its own SDK callbacks fire.
  void simulateJoined() { emit joined(); }
  void simulateJoinFailed(const QString &reason) { emit joinFailed(reason); }
  void simulateLeft() { emit left(); }
  void simulateRemoteParticipantConnected() { emit remoteParticipantConnected(); }
  void simulateReconnecting() { emit reconnecting(); }
  void simulateReconnected() { emit reconnected(); }
  void simulateConnectionLost(const QString &reason) { emit connectionLost(reason); }

  QString mLastJoinUrl;
  QString mLastJoinToken;
  int mJoinCallCount{0};
  int mLeaveCallCount{0};
};

} // namespace pcm::video::test
