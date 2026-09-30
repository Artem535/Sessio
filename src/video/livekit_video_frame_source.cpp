#include "livekit_video_frame_source.h"

namespace pcm::video {

LiveKitVideoFrameSource::LiveKitVideoFrameSource(QObject *parent) : VideoFrameSource(parent) {}

LiveKitVideoFrameSource::~LiveKitVideoFrameSource() { detach(); }

void LiveKitVideoFrameSource::attachTrack(const std::shared_ptr<livekit::Track> &track) {
  detach();
  if (!track) return;
  livekit::VideoStream::Options options;
  options.capacity = 2;
  options.format = livekit::VideoBufferType::RGBA;
  mStream = livekit::VideoStream::fromTrack(track, options);
  if (mStream) {
    mRunning.store(true);
    mReaderThread = std::thread(&LiveKitVideoFrameSource::readerLoop, this);
  }
}

void LiveKitVideoFrameSource::detach() {
  mRunning.store(false);
  if (mStream) mStream->close(); // wake read() before joining
  if (mReaderThread.joinable()) mReaderThread.join();
  mStream.reset();
  clear(); // after join: an old producer can no longer replace the clear
}

void LiveKitVideoFrameSource::readerLoop() {
  livekit::VideoFrameEvent event;
  while (mRunning.load() && mStream->read(event)) {
    const auto &frame = event.frame;
    const QImage image(frame.data(), static_cast<int>(frame.width()),
                       static_cast<int>(frame.height()), QImage::Format_RGBA8888);
    submitFrame(image); // mailbox copies before SDK buffer can be reused
  }
}

} // namespace pcm::video
