#include "busy_spinner.h"

#include <QPainter>
#include <QPropertyAnimation>

namespace pcm::widgets {

namespace {
constexpr int kArcSpanDegrees = 270;
constexpr int kRotationPeriodMs = 1000;
} // namespace

BusySpinner::BusySpinner(QWidget *parent) : QWidget(parent), mAnimation(new QPropertyAnimation(this, "angle", this)) {
  setFixedSize(16, 16);
  mAnimation->setStartValue(0);
  mAnimation->setEndValue(360);
  mAnimation->setDuration(kRotationPeriodMs);
  mAnimation->setLoopCount(-1);
}

void BusySpinner::setAngle(int angle) {
  mAngle = angle;
  update();
}

bool BusySpinner::isAnimating() const {
  return mAnimation->state() == QAbstractAnimation::Running;
}

void BusySpinner::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  QPen pen(palette().color(QPalette::WindowText));
  pen.setWidth(2);
  pen.setCapStyle(Qt::RoundCap);
  painter.setPen(pen);
  const QRectF arcRect = rect().adjusted(1, 1, -1, -1);
  // QPainter::drawArc()'s angles are in 1/16ths of a degree and increase
  // counter-clockwise from the 3-o'clock position.
  painter.drawArc(arcRect, -mAngle * 16, kArcSpanDegrees * 16);
}

void BusySpinner::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  mAnimation->start();
}

void BusySpinner::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  mAnimation->stop();
}

} // namespace pcm::widgets
