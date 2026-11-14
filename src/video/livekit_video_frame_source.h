#pragma once

#include "video_frame_source.h"
#include <atomic>
#include <livekit/video_stream.h>
#include <memory>
#include <thread>

namespace pcm::video {

class LiveKitVideoFrameSource final : public VideoFrameSource {
  Q_OBJECT
public:
  explicit LiveKitVideoFrameSource(QObject *parent = nullptr);
  ~LiveKitVideoFrameSource() override;
  void attachTrack(const std::shared_ptr<livekit::Track> &track);
  void detach();

private:
  std::shared_ptr<livekit::VideoStream> mStream;
  std::thread mReaderThread;
  std::atomic<bool> mRunning{false};
  void readerLoop();
};

} // namespace pcm::video
