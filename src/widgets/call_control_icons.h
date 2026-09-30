#pragma once

#include <QIcon>

namespace pcm::widgets {

// Self-drawn (no external SVG asset) glyph icons for the in-call control
// bar. Composed from basic QPainter primitives so the icon set never
// depends on a network fetch or a new third-party license.
[[nodiscard]] QIcon microphoneIcon(bool enabled);
[[nodiscard]] QIcon cameraIcon(bool enabled);
[[nodiscard]] QIcon devicesIcon();
[[nodiscard]] QIcon fullscreenIcon(bool active);

} // namespace pcm::widgets
