#pragma once

#include <QDate>
#include <QLabel>
#include <QMap>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QRect>
#include <QResizeEvent>
#include <QTextCharFormat>
#include <QToolButton>
#include <QWidget>

class RoundedCalendarWidget final : public QWidget {
  Q_OBJECT

public:
  explicit RoundedCalendarWidget(QWidget *parent = nullptr);

  void setSelectedDate(const QDate &date);
  [[nodiscard]] QDate selectedDate() const;
  void setDateTextFormat(const QDate &date, const QTextCharFormat &format);

signals:
  void clicked(const QDate &date);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  void syncNavigationControls() const;
  [[nodiscard]] QRect gridRect() const;
  [[nodiscard]] QRect cellRect(int row, int column) const;
  [[nodiscard]] QDate firstVisibleDate() const;

  QDate mSelectedDate;
  QDate mShownMonth;
  QMap<QDate, QTextCharFormat> mDateFormats;
  QToolButton *mPreviousMonthButton = nullptr;
  QToolButton *mNextMonthButton = nullptr;
  QLabel *mMonthYearLabel = nullptr;
};
