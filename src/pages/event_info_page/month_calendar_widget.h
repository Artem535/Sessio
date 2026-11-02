#pragma once

#include "qtimeline_model.h"
#include <QWidget>
#include <utility>

class QGridLayout;

class MonthCalendarWidget final : public QWidget {
  Q_OBJECT
public:
  explicit MonthCalendarWidget(QWidget *parent = nullptr);
  void setMonth(QDate month);
  void setSelectedDate(QDate date);
  void setEvents(QVector<DuckEvent> events);
  // Single combined update: builds the grid at most once, and not at all when
  // neither the month nor the events changed.
  void setMonthAndEvents(QDate month, QVector<DuckEvent> events);
  // First/last date the grid shows for `month` in the current locale.
  static std::pair<QDate, QDate> visibleRange(QDate month);
  QDate firstVisibleDate() const;
  QDate lastVisibleDate() const;
  // Number of full grid builds so far (regression guard against rebuild storms).
  int rebuildCount() const;
  // Re-reads the work/personal event colors from settings and repaints chips.
  void refreshAppearance();

signals:
  void dateSelected(QDate date);
  void eventSelected(DuckEvent event);

protected:
  void changeEvent(QEvent *event) override;

private:
  void rebuild();
  QGridLayout *mGrid;
  QDate mMonth;
  QDate mSelectedDate;
  QDate mFirst;
  QDate mLast;
  QVector<DuckEvent> mEvents;
  int mRebuildCount = 0;
};
