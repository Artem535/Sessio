// Throwaway spike for issue #77 — not production quality.
#include "frame_convert.h"

#include <cstdint>
#include <cstring>

namespace pcm::spike {

livekit::VideoFrame videoFrameToLiveKitRGBA(const QImage &image) {
  const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);

  auto frame = livekit::VideoFrame::create(rgba.width(), rgba.height(),
                                           livekit::VideoBufferType::RGBA);

  std::uint8_t *dst = frame.data();
  const std::size_t dstStride = static_cast<std::size_t>(rgba.width()) * 4;
  for (int y = 0; y < rgba.height(); ++y) {
    std::memcpy(dst + static_cast<std::size_t>(y) * dstStride,
               rgba.constScanLine(y), dstStride);
  }
  return frame;
}

}  // namespace pcm::spike
