#include "pipewire_screen_stream.h"

#include <QDateTime>
#include <QLoggingCategory>

#include <algorithm>
#include <mutex>
#include <spa/param/buffers.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <unistd.h>
#include <utility>

Q_LOGGING_CATEGORY(lcPipeWireScreen, "pcm.video.pipewire")

namespace pcm::video {

namespace {

const pw_stream_events kStreamEvents = [] {
  pw_stream_events events{};
  events.version = PW_VERSION_STREAM_EVENTS;
  return events;
}();

QImage::Format imageFormat(spa_video_format format) {
  // Byte order in memory; QImage's 32-bit formats are native-endian words.
  switch (format) {
  case SPA_VIDEO_FORMAT_BGRx: return QImage::Format_RGB32;
  case SPA_VIDEO_FORMAT_BGRA: return QImage::Format_ARGB32;
  case SPA_VIDEO_FORMAT_RGBx: return QImage::Format_RGBX8888;
  case SPA_VIDEO_FORMAT_RGBA: return QImage::Format_RGBA8888;
  default: return QImage::Format_Invalid;
  }
}

} // namespace

PipeWireScreenStream::PipeWireScreenStream(FrameHandler onFrame, ErrorHandler onError, int minFrameIntervalMs)
    : mOnFrame(std::move(onFrame)), mOnError(std::move(onError)), mMinFrameIntervalMs(minFrameIntervalMs) {
  static std::once_flag initialized;
  std::call_once(initialized, [] { pw_init(nullptr, nullptr); });
}

PipeWireScreenStream::~PipeWireScreenStream() { close(); }

bool PipeWireScreenStream::open(int remoteFd, quint32 nodeId, QString *error) {
  close();
  auto failWith = [&](const QString &message) {
    if (error) *error = message;
    close();
    return false;
  };

  mLoop = pw_thread_loop_new("sessio-screen", nullptr);
  if (!mLoop) {
    ::close(remoteFd);
    return failWith(QStringLiteral("Could not create the PipeWire thread."));
  }
  mContext = pw_context_new(pw_thread_loop_get_loop(mLoop), nullptr, 0);
  if (!mContext) {
    ::close(remoteFd);
    return failWith(QStringLiteral("Could not create the PipeWire context."));
  }
  if (pw_thread_loop_start(mLoop) < 0) {
    ::close(remoteFd);
    return failWith(QStringLiteral("Could not start the PipeWire thread."));
  }

  pw_thread_loop_lock(mLoop);
  // The core owns the descriptor from here on.
  mCore = pw_context_connect_fd(mContext, remoteFd, nullptr, 0);
  if (!mCore) {
    pw_thread_loop_unlock(mLoop);
    return failWith(QStringLiteral("Could not connect to the PipeWire screen stream."));
  }
  static const pw_core_events coreEvents = [] {
    pw_core_events events{};
    events.version = PW_VERSION_CORE_EVENTS;
    events.error = &PipeWireScreenStream::onCoreError;
    return events;
  }();
  pw_core_add_listener(mCore, &mCoreListener, &coreEvents, this);

  mStream = pw_stream_new(mCore, "Sessio screen share",
                          pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture",
                                            PW_KEY_MEDIA_ROLE, "Screen", nullptr));
  if (!mStream) {
    pw_thread_loop_unlock(mLoop);
    return failWith(QStringLiteral("Could not create the PipeWire stream."));
  }
  static const pw_stream_events streamEvents = [] {
    pw_stream_events events = kStreamEvents;
    events.state_changed = &PipeWireScreenStream::onStateChanged;
    events.param_changed = &PipeWireScreenStream::onParamChanged;
    events.process = &PipeWireScreenStream::onProcess;
    return events;
  }();
  pw_stream_add_listener(mStream, &mStreamListener, &streamEvents, this);

  uint8_t buffer[1024];
  spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
  spa_rectangle defaultSize{1920, 1080};
  spa_rectangle minSize{1, 1};
  spa_rectangle maxSize{16384, 16384};
  spa_fraction defaultRate{30, 1};
  spa_fraction minRate{0, 1};
  spa_fraction maxRate{144, 1};
  const spa_pod *params[] = {static_cast<const spa_pod *>(spa_pod_builder_add_object(
      &builder, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
      SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
      SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
      SPA_FORMAT_VIDEO_format,
      SPA_POD_CHOICE_ENUM_Id(5, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_RGBx,
                             SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_RGBA),
      SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&defaultSize, &minSize, &maxSize),
      SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&defaultRate, &minRate, &maxRate)))};
  const int result = pw_stream_connect(mStream, PW_DIRECTION_INPUT, nodeId,
                                       static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
                                                                    PW_STREAM_FLAG_MAP_BUFFERS),
                                       params, 1);
  pw_thread_loop_unlock(mLoop);
  if (result < 0) return failWith(QStringLiteral("Could not connect to the shared screen (%1).").arg(result));
  return true;
}

void PipeWireScreenStream::close() {
  if (mLoop) pw_thread_loop_stop(mLoop);
  // The thread is gone: the objects can be destroyed without its lock.
  if (mStream) {
    spa_hook_remove(&mStreamListener);
    pw_stream_destroy(std::exchange(mStream, nullptr));
  }
  if (mCore) {
    spa_hook_remove(&mCoreListener);
    pw_core_disconnect(std::exchange(mCore, nullptr));
  }
  if (mContext) pw_context_destroy(std::exchange(mContext, nullptr));
  if (mLoop) pw_thread_loop_destroy(std::exchange(mLoop, nullptr));
  mStreaming = false;
  mFormat = {};
}

void PipeWireScreenStream::onParamChanged(void *data, uint32_t id, const struct spa_pod *param) {
  auto *self = static_cast<PipeWireScreenStream *>(data);
  if (!param || id != SPA_PARAM_Format) return;
  uint32_t mediaType = 0;
  uint32_t mediaSubtype = 0;
  if (spa_format_parse(param, &mediaType, &mediaSubtype) < 0 || mediaType != SPA_MEDIA_TYPE_video ||
      mediaSubtype != SPA_MEDIA_SUBTYPE_raw)
    return;
  spa_video_info_raw format{};
  if (spa_format_video_raw_parse(param, &format) < 0) return;
  self->mFormat = format;
  qCDebug(lcPipeWireScreen) << "format" << format.format << format.size.width << "x" << format.size.height;

  uint8_t buffer[256];
  spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
  const spa_pod *params[] = {static_cast<const spa_pod *>(spa_pod_builder_add_object(
      &builder, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
      SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_MemFd))))};
  pw_stream_update_params(self->mStream, params, 1);
}

void PipeWireScreenStream::onProcess(void *data) { static_cast<PipeWireScreenStream *>(data)->readFrame(); }

void PipeWireScreenStream::readFrame() {
  pw_buffer *buffer = nullptr;
  // Only the newest frame matters; return the older ones straight away.
  while (pw_buffer *next = pw_stream_dequeue_buffer(mStream)) {
    if (buffer) pw_stream_queue_buffer(mStream, buffer);
    buffer = next;
  }
  if (!buffer) return;

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  const QImage::Format format = imageFormat(mFormat.format);
  const spa_data &plane = buffer->buffer->datas[0];
  if (now - mLastFrameMs >= mMinFrameIntervalMs && format != QImage::Format_Invalid && plane.data && plane.chunk &&
      plane.chunk->size > 0 && !(plane.chunk->flags & SPA_CHUNK_FLAG_CORRUPTED)) {
    const int width = static_cast<int>(mFormat.size.width);
    const int height = static_cast<int>(mFormat.size.height);
    const int stride = plane.chunk->stride > 0 ? plane.chunk->stride : width * 4;
    const quint64 offset = plane.chunk->offset % std::max<quint32>(plane.maxsize, 1);
    if (width > 0 && height > 0 && offset + quint64(stride) * height <= plane.maxsize) {
      const auto *pixels = static_cast<const uchar *>(plane.data) + offset;
      // copy(): the buffer goes back to the compositor below.
      mOnFrame(QImage(pixels, width, height, stride, format).copy());
      mLastFrameMs = now;
    }
  }
  pw_stream_queue_buffer(mStream, buffer);
}

void PipeWireScreenStream::onStateChanged(void *data, pw_stream_state oldState, pw_stream_state state,
                                          const char *error) {
  auto *self = static_cast<PipeWireScreenStream *>(data);
  qCDebug(lcPipeWireScreen) << "stream state" << pw_stream_state_as_string(oldState) << "->"
                            << pw_stream_state_as_string(state);
  if (state == PW_STREAM_STATE_STREAMING) self->mStreaming = true;
  if (state == PW_STREAM_STATE_ERROR) {
    self->mOnError(QStringLiteral("The screen stream failed: %1")
                       .arg(error ? QString::fromUtf8(error) : QStringLiteral("unknown error")));
  } else if (state == PW_STREAM_STATE_UNCONNECTED && self->mStreaming) {
    self->mStreaming = false;
    self->mOnError(QStringLiteral("The screen stream ended."));
  }
}

void PipeWireScreenStream::onCoreError(void *data, uint32_t id, int seq, int res, const char *message) {
  Q_UNUSED(seq);
  if (id != PW_ID_CORE) return;
  auto *self = static_cast<PipeWireScreenStream *>(data);
  self->mOnError(QStringLiteral("PipeWire error %1: %2").arg(res).arg(message ? QString::fromUtf8(message) : QString()));
}

} // namespace pcm::video
