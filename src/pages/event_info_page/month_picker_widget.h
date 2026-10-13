#pragma once

#include <QDate>
#include <QWidget>

class QLabel;
class QToolButton;

// Month-mode counterpart of RoundedCalendarWidget: the same rounded card with
// "‹ 2026 ›" year navigation and a 3x4 grid of month tiles.
class MonthPickerWidget final : public QWidget {
  Q_OBJECT
public:
  explicit MonthPickerWidget(QWidget *parent = nullptr);

  // The month shown by the month grid. The picker also jumps to its year.
  void setDisplayedMonth(QDate month);
  [[nodiscard]] QDate displayedMonth() const;
  // Year currently listed (can differ from the displayed month's year while browsing).
  [[nodiscard]] int shownYear() const;
  void setShownYear(int year);

signals:
  // First day of the chosen month.
  void monthSelected(QDate month);

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  void relayout();
  void refreshLabels();

  QDate mDisplayed;
  int mYear;
  QToolButton *mPrevious;
  QToolButton *mNext;
  QLabel *mYearLabel;
  QToolButton *mTiles[12] = {};
};
