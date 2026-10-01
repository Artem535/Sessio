#pragma once

namespace pcm::video {

// Drives VideoSession's state machine. See docs/video-roadmap.md §5.5 and
// docs/asciidoc/14-video-session-state-machine-adr.adoc.
enum class VideoSessionState {
  NoMeeting,
  Provisioned,
  PrejoinCheck,
  Joining,
  WaitingForParticipants,
  Connected,
  Reconnecting,
  Leaving,
  Ended,
  Failed,
};

} // namespace pcm::video
