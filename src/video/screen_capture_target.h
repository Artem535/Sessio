#pragma once

#include <QCapturableWindow>
#include <QPointer>
#include <QScreen>

namespace pcm::video {

// What the user chose to share: a whole screen or a single window.
struct ScreenCaptureTarget {
  QPointer<QScreen> screen;
  QCapturableWindow window;
  [[nodiscard]] bool isValid() const { return screen || window.isValid(); }
};

// True when the desktop's own picker chooses what is shared (the Wayland
// ScreenCast portal): a requested target then only asks to share.
[[nodiscard]] bool screenCaptureUsesSystemPicker();

} // namespace pcm::video
