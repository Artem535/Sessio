#pragma once

#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <QString>

// Accent colour helpers. The accent lives in exactly one place, the Qlementine
// theme (resources/themes/qlementine_dark.json: primaryColor), which Qlementine
// publishes as QPalette::Highlight / HighlightedText. Custom-painted widgets read
// it from the palette instead of hard-coding a literal, so a theme change moves
// the whole UI together.
namespace pcm::widgets {

// Main accent (selection pill, today marker, chart bars).
inline QColor accentColor(const QPalette &palette = QGuiApplication::palette()) {
  return palette.color(QPalette::Highlight);
}

// Text/glyph colour that stays readable on top of accentColor().
inline QColor onAccentColor(const QPalette &palette = QGuiApplication::palette()) {
  return palette.color(QPalette::HighlightedText);
}

// Accent mixed toward white: a soft tint for text drawn on the dark surface
// (today's day number). ratio is the white share, 0..1.
inline QColor accentSoftColor(const QPalette &palette = QGuiApplication::palette(),
                              qreal ratio = 0.6) {
  const QColor accent = accentColor(palette);
  const auto mix = [ratio](int channel) {
    return static_cast<int>(channel + (255 - channel) * ratio + 0.5);
  };
  return QColor(mix(accent.red()), mix(accent.green()), mix(accent.blue()));
}

// CSS colour for style sheets ("rgba(r, g, b, a)"), a in 0..1.
inline QString cssRgba(const QColor &color, qreal alpha = 1.0) {
  return QStringLiteral("rgba(%1, %2, %3, %4)")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(alpha, 0, 'f', 2);
}

}  // namespace pcm::widgets
