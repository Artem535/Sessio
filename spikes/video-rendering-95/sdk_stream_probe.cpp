// Throwaway offline LiveKit 1.12.0 per-track frame routing probe for #95.
// LocalVideoTrack only: this does NOT verify remote room subscriptions,
// WebRTC decoding, network conditions, or performance of a real group call.
#include <livekit/livekit.h>
#include <livekit/local_video_track.h>
#include <livekit/video_frame.h>
#include <livekit/video_source.h>
#include <livekit/video_stream.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

struct Channel {
  std::shared_ptr<livekit::VideoSource> source;
  std::shared_ptr<livekit::LocalVideoTrack> track;
  std::shared_ptr<livekit::VideoStream> stream;
  std::thread reader;
  std::atomic<int> frames{0};
  std::atomic<int> wrongFrames{0};
  livekit::VideoFrame frame;

  explicit Channel(int id)
      : source(std::make_shared<livekit::VideoSource>(64, 48)),
        track(livekit::LocalVideoTrack::createLocalVideoTrack(
            "synthetic-" + std::to_string(id), source)),
        frame(livekit::VideoFrame::create(64, 48, livekit::VideoBufferType::RGBA)) {
    const int marker = 30 + id * 20;
    for (std::size_t pixel = 0; pixel < frame.dataSize(); pixel += 4) {
      frame.data()[pixel] = marker;
      frame.data()[pixel + 1] = marker;
      frame.data()[pixel + 2] = marker;
      frame.data()[pixel + 3] = 255;
    }
    livekit::VideoStream::Options options;
    options.capacity = 2;
    options.format = livekit::VideoBufferType::RGBA;
    stream = livekit::VideoStream::fromTrack(track, options);
    reader = std::thread([this, marker] {
      livekit::VideoFrameEvent event;
      while (stream->read(event)) {
        const auto &received = event.frame;
        bool wrong = received.width() != 64 || received.height() != 48 ||
                     received.type() != livekit::VideoBufferType::RGBA ||
                     received.dataSize() != 64 * 48 * 4;
        if (!wrong) {
          // Tolerance for a possible I420 round trip in the local SDK path.
          // Adjacent tracks differ by 20, so a misroute still fails.
          for (std::size_t pixel = 0; pixel < received.dataSize(); pixel += 4) {
            for (int component = 0; component < 3; ++component) {
              wrong |= std::abs(static_cast<int>(received.data()[pixel + component]) - marker) > 4;
            }
          }
        }
        if (wrong) {
          wrongFrames.fetch_add(1);
        }
        frames.fetch_add(1);
      }
    });
  }

  void stop() {
    stream->close();
    if (reader.joinable()) {
      reader.join();
    }
  }
  ~Channel() { stop(); }
};

int main() {
  livekit::initialize(livekit::LogLevel::Error);
  bool valid = true;
  try {
    std::vector<std::unique_ptr<Channel>> channels;
    for (int id = 0; id < 10; ++id) {
      channels.push_back(std::make_unique<Channel>(id));
    }
    // Send 90 distinguishable frames per local track at 30 fps.
    auto deadline = Clock::now();
    for (int sequence = 0; sequence < 90; ++sequence) {
      for (auto &channel : channels) {
        channel->source->captureFrame(channel->frame);
      }
      deadline += std::chrono::nanoseconds(1000000000 / 30);
      std::this_thread::sleep_until(deadline);
    }
    const auto closeStart = Clock::now();
    for (auto &channel : channels) {
      channel->stop();
    }
    const double closeMs = std::chrono::duration<double, std::milli>(Clock::now() - closeStart).count();
    for (auto &channel : channels) {
      valid &= channel->frames.load() >= 60 && channel->wrongFrames.load() == 0;
    }
    std::cout << "{\"prototype\":true,\"remote_subscriptions_verified\":false,"
              << "\"local_track_routing_valid\":" << (valid ? "true" : "false")
              << ",\"close_join_ms\":" << closeMs << ",\"tracks\":[";
    for (std::size_t id = 0; id < channels.size(); ++id) {
      if (id) {
        std::cout << ',';
      }
      std::cout << "{\"id\":" << id << ",\"frames\":" << channels[id]->frames.load()
                << ",\"wrong_frames\":" << channels[id]->wrongFrames.load() << '}';
    }
    std::cout << "]}\n";
  } catch (const std::exception &error) {
    std::cerr << "Offline SDK probe failed: " << error.what() << '\n';
    valid = false;
  }
  livekit::shutdown();
  return valid ? 0 : 1;
}
