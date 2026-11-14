#pragma once

#include <QWidget>

class QPropertyAnimation;

namespace pcm::widgets {

// Small indeterminate circular progress indicator: a rotating arc drawn via
// QPainter. The rotation animation only runs while the widget is actually
// visible (started on showEvent(), stopped on hideEvent()) so a spinner on
// a screen that isn't currently shown costs nothing.
class BusySpinner final : public QWidget {
  Q_OBJECT
  Q_PROPERTY(int angle READ angle WRITE setAngle)

public:
  explicit BusySpinner(QWidget *parent = nullptr);

  [[nodiscard]] int angle() const { return mAngle; }
  void setAngle(int angle);

  [[nodiscard]] bool isAnimating() const;

protected:
  void paintEvent(QPaintEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  int mAngle{0};
  QPropertyAnimation *mAnimation;
};

} // namespace pcm::widgets
