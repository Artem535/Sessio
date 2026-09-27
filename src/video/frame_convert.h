#pragma once

#include <QImage>
#include <livekit/video_frame.h>

namespace pcm::video {

// Converts a QImage to a livekit::VideoFrame in RGBA byte order. RGBA is used
// directly (no I420 conversion) since LiveKit's VideoSource accepts
// RGBA-format frames, matching the LiveKit SDK's own examples.
[[nodiscard]] livekit::VideoFrame videoFrameToLiveKitRGBA(const QImage &image);

} // namespace pcm::video
