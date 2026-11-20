#pragma once

#include "screen_capture.h"

#include <QObject>
#include <functional>
#include <memory>

namespace pcm::video {

// Owns one screen share: the capture, and whether its track is published.
// The track is published only after the capture delivers a frame, so a
// cancelled picker or a portal failure never shows the call a black screen.
class ScreenShareController final : public QObject {
  Q_OBJECT
public:
  // Publishes a screen track fed by the capture's video source; throws on failure.
  using Publish = std::function<void(ScreenCapture &)>;
  // Unpublishes that track; must not throw.
  using Unpublish = std::function<void()>;

  ScreenShareController(Publish publish, Unpublish unpublish, QObject *parent = nullptr);
  ~ScreenShareController() override;

  // Replaces the current share. Nothing is published until the capture starts.
  void start(std::unique_ptr<ScreenCapture> capture, const ScreenCaptureTarget &target);
  // Stops the share the user asked to end; unpublishes a live track.
  void stop();
  // Stops capturing without unpublishing or signals: the call is being torn down.
  void discard();

  [[nodiscard]] bool isSharing() const { return mLive; }
  [[nodiscard]] bool isPending() const { return mCapture && !mLive; }

signals:
  void sharingChanged(bool sharing);
  // A user-facing reason; the share is already torn down when this is emitted.
  void failed(QString reason);

private:
  void onStarted();
  void onCaptureFailed(const QString &reason);
  // Stops the capture and frees it once control is back in the event loop.
  void releaseCapture();

  Publish mPublish;
  Unpublish mUnpublish;
  std::unique_ptr<ScreenCapture> mCapture;
  bool mLive{false};
};

} // namespace pcm::video
