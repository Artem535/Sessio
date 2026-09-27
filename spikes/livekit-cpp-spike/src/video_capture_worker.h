#pragma once

// Throwaway spike for issue #77 — not production quality.

#include <QObject>
#include <QVideoFrame>

#include <atomic>
#include <memory>

#include "livekit/livekit.h"

// Lives on VideoCaptureAdapter's dedicated worker QThread (see
// video_capture_adapter.h/.cpp). Does the CPU-heavy per-frame work that used
// to run synchronously on the GUI thread inside QVideoSink::videoFrameChanged:
// QVideoFrame -> QImage conversion, RGBA repack (pcm::spike::videoFrameToLiveKitRGBA,
// which memcpy's the whole buffer), and the livekit::VideoSource::captureFrame()
// FFI call. VideoCaptureAdapter's GUI-thread slot only forwards the frame here
// via a queued QMetaObject::invokeMethod() — this class never touches anything
// on the GUI thread itself.
class VideoCaptureWorker final : public QObject {
  Q_OBJECT

public:
  VideoCaptureWorker(std::shared_ptr<livekit::VideoSource> videoSource,
                      std::atomic<int> &framesCaptured);

  // Called from VideoCaptureAdapter (GUI thread) before asking the worker
  // thread's event loop to quit. Guards against a processFrame() call that
  // was already queued when stop() ran: it will see mRunning == false and
  // no-op instead of calling into mVideoSource mid-teardown. Same defensive
  // pattern as RemoteAudioPlayer::mRunning / RemoteVideoRenderer::mRunning.
  void setRunning(bool running) { mRunning.store(running); }

public slots:
  // Invoked via Qt::QueuedConnection from VideoCaptureAdapter::onVideoFrameChanged.
  // QVideoFrame is explicitly shared and has no thread affinity of its own, so
  // passing it by value across the queued call is safe (Qt docs: "QVideoFrame
  // is explicitly shared, so any change made to a video frame will also apply
  // to any copies").
  void processFrame(const QVideoFrame &frame);

signals:
  // Forwarded by VideoCaptureAdapter to its own frameCaptured() signal so the
  // existing public contract (SpikeWindow reads framesCaptured(), not this
  // signal, but the signal itself is part of VideoCaptureAdapter's API) is
  // unchanged.
  void frameCaptured();

private:
  std::shared_ptr<livekit::VideoSource> mVideoSource;
  // Reference to VideoCaptureAdapter::mFramesCaptured. Valid for this
  // worker's whole lifetime: VideoCaptureAdapter explicitly destroys the
  // worker (in stop()) before mFramesCaptured itself could go away.
  std::atomic<int> &mFramesCaptured;
  std::atomic<bool> mRunning{true};
};
