#pragma once

#include "qtimeline_model.h"
#include <QWidget>

class QGridLayout;

class MonthCalendarWidget final : public QWidget {
  Q_OBJECT
public:
  explicit MonthCalendarWidget(QWidget *parent = nullptr);
  void setMonth(QDate month);
  void setSelectedDate(QDate date);
  void setEvents(QVector<DuckEvent> events);
  QDate firstVisibleDate() const;
  QDate lastVisibleDate() const;

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
};
