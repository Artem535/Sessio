#include "call_control_icons.h"

#include <QPainter>
#include <QPixmap>

namespace pcm::widgets {

namespace {
constexpr int kIconSize = 24;

template <typename Draw>
QIcon renderIcon(bool danger, Draw draw) {
  QPixmap pixmap(kIconSize, kIconSize);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  const QColor color = danger ? QColor(220, 53, 69) : QColor(230, 230, 230);
  draw(painter, color);
  return QIcon(pixmap);
}

void drawSlash(QPainter &painter, const QColor &color) {
  painter.setPen(QPen(color, 2));
  painter.drawLine(QPointF(4, 4), QPointF(20, 20));
}
} // namespace

QIcon microphoneIcon(bool enabled) {
  return renderIcon(!enabled, [enabled](QPainter &painter, const QColor &color) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(9, 3, 6, 12), 3, 3);
    painter.setPen(QPen(color, 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(QRectF(6, 9, 12, 12), 0, -180 * 16);
    painter.drawLine(QPointF(12, 21), QPointF(12, 18));
    painter.drawLine(QPointF(8, 21), QPointF(16, 21));
    if (!enabled) {
      drawSlash(painter, color);
    }
  });
}

QIcon cameraIcon(bool enabled) {
  return renderIcon(!enabled, [enabled](QPainter &painter, const QColor &color) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(2, 7, 14, 10), 2, 2);
    QPolygonF lens;
    lens << QPointF(16, 10) << QPointF(22, 6) << QPointF(22, 18) << QPointF(16, 14);
    painter.drawPolygon(lens);
    if (!enabled) {
      drawSlash(painter, color);
    }
  });
}

QIcon devicesIcon() {
  return renderIcon(false, [](QPainter &painter, const QColor &color) {
    const int knobX[3] = {8, 16, 10};
    for (int row = 0; row < 3; ++row) {
      const int y = 6 + row * 6;
      painter.setPen(QPen(color, 2));
      painter.drawLine(QPointF(3, y), QPointF(21, y));
      painter.setPen(Qt::NoPen);
      painter.setBrush(color);
      painter.drawEllipse(QPointF(knobX[row], y), 2.5, 2.5);
    }
  });
}

QIcon fullscreenIcon(bool active) {
  return renderIcon(false, [active](QPainter &painter, const QColor &color) {
    painter.setPen(QPen(color, 2));
    const int o = active ? 7 : 3;
    const int l = 5;
    painter.drawLine(QPointF(o, o + l), QPointF(o, o));
    painter.drawLine(QPointF(o, o), QPointF(o + l, o));
    painter.drawLine(QPointF(24 - o, o + l), QPointF(24 - o, o));
    painter.drawLine(QPointF(24 - o, o), QPointF(24 - o - l, o));
    painter.drawLine(QPointF(o, 24 - o - l), QPointF(o, 24 - o));
    painter.drawLine(QPointF(o, 24 - o), QPointF(o + l, 24 - o));
    painter.drawLine(QPointF(24 - o, 24 - o - l), QPointF(24 - o, 24 - o));
    painter.drawLine(QPointF(24 - o, 24 - o), QPointF(24 - o - l, 24 - o));
  });
}

} // namespace pcm::widgets
