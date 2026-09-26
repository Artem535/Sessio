#include "frame_convert.h"

namespace pcm::video {

livekit::VideoFrame videoFrameToLiveKitRGBA(const QImage &image) {
  const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
  const auto width = static_cast<uint32_t>(rgba.width());
  const auto height = static_cast<uint32_t>(rgba.height());

  auto frame = livekit::VideoFrame::create(width, height, livekit::VideoBufferType::RGBA);

  const auto stride = width * 4;
  for (uint32_t y = 0; y < height; ++y) {
    std::memcpy(frame.data() + y * stride, rgba.constScanLine(static_cast<int>(y)), stride);
  }

  return frame;
}

} // namespace pcm::video
