#pragma once

#include "screen_capture_target.h"

#include <QObject>
#include <memory>

namespace livekit {
class VideoSource;
}

namespace pcm::video {

// A running screen or window capture feeding a livekit::VideoSource.
// started() and captureFailed() are always delivered from the event loop,
// never from inside start(), so a listener may destroy the capture.
class ScreenCapture : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;

  [[nodiscard]] virtual std::shared_ptr<livekit::VideoSource> videoSource() const = 0;
  // Begins capturing. False when the target is unusable; no signal follows then.
  virtual bool start(const ScreenCaptureTarget &target) = 0;
  // Ends the capture and releases every OS resource (portal session, stream).
  virtual void stop() = 0;

signals:
  // The first frame reached the video source: the capture really works.
  void started();
  // The capture never started (picker cancelled, portal error) or ended.
  void captureFailed(QString reason);
};

} // namespace pcm::video
