#pragma once

// Throwaway spike for issue #77 — not production quality.

#include <QImage>

#include "livekit/livekit.h"

namespace pcm::spike {

// Converts a QImage (any Qt pixel format) to a livekit::VideoFrame in RGBA
// byte order, matching what QEventItem-style RGBA-capable VideoSources
// expect (see basic_room/capture_utils.cpp in the LiveKit examples: RGBA is
// directly supported, no I420 conversion required).
[[nodiscard]] livekit::VideoFrame videoFrameToLiveKitRGBA(const QImage &image);

}  // namespace pcm::spike
