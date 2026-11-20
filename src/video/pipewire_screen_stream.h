#pragma once

#include <QImage>
#include <QString>
#include <functional>
#include <pipewire/pipewire.h>
#include <spa/param/video/raw.h>

namespace pcm::video {

// Reads a screen-cast node from a PipeWire remote (as handed out by the
// ScreenCast portal) on PipeWire's own thread. Only shared-memory buffers are
// negotiated, so every frame can be copied into a QImage without a GPU.
class PipeWireScreenStream final {
public:
  // Both are called on the PipeWire thread.
  using FrameHandler = std::function<void(QImage)>;
  using ErrorHandler = std::function<void(QString)>;

  PipeWireScreenStream(FrameHandler onFrame, ErrorHandler onError, int minFrameIntervalMs);
  ~PipeWireScreenStream();
  PipeWireScreenStream(const PipeWireScreenStream &) = delete;
  PipeWireScreenStream &operator=(const PipeWireScreenStream &) = delete;

  // Takes ownership of remoteFd. On failure returns false and fills error.
  bool open(int remoteFd, quint32 nodeId, QString *error);
  // Stops the PipeWire thread; no handler runs after this returns.
  void close();

private:
  static void onParamChanged(void *data, uint32_t id, const struct spa_pod *param);
  static void onProcess(void *data);
  static void onStateChanged(void *data, pw_stream_state oldState, pw_stream_state state, const char *error);
  static void onCoreError(void *data, uint32_t id, int seq, int res, const char *message);
  void readFrame();

  FrameHandler mOnFrame;
  ErrorHandler mOnError;
  int mMinFrameIntervalMs;
  pw_thread_loop *mLoop{nullptr};
  pw_context *mContext{nullptr};
  pw_core *mCore{nullptr};
  pw_stream *mStream{nullptr};
  spa_hook mCoreListener{};
  spa_hook mStreamListener{};
  spa_video_info_raw mFormat{};
  qint64 mLastFrameMs{0};
  bool mStreaming{false};
};

} // namespace pcm::video
