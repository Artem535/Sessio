#include "month_calendar_widget.h"

#include <QEvent>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimeZone>
#include <QToolButton>
#include <algorithm>
#include <functional>

namespace {

QString eventLabel(const DuckEvent &event) {
  const auto start = QDateTime::fromMSecsSinceEpoch(event.start_date.value_or(0),
                                                  QTimeZone::systemTimeZone());
  auto title = QString::fromStdString(event.name.value_or(""));
  const auto client = QString::fromStdString(event.client_name.value_or(""));
  if (!client.isEmpty() && client != title) {
    title += QStringLiteral(" · ") + client;
  }
  return start.toString(QStringLiteral("HH:mm")) + QStringLiteral("  ") + title;
}

class EventButton final : public QPushButton {
public:
  EventButton(const QString &label, QWidget *parent) : QPushButton(parent), mLabel(label) {
    setObjectName(QStringLiteral("monthEvent"));
    setToolTip(label);
    setAccessibleName(label);
    setFlat(true);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  }

protected:
  void resizeEvent(QResizeEvent *event) override {
    QPushButton::resizeEvent(event);
    setText(fontMetrics().elidedText(mLabel, Qt::ElideRight, std::max(0, width() - 12)));
  }

private:
  QString mLabel;
};

// Real buttons preserve style, keyboard focus and accessibility under Qlementine.
// Only the cell background/border is painted, using the current theme palette.
class DayCell final : public QWidget {
public:
  DayCell(const QDate &date, const bool adjacent, const bool selected,
          QVector<DuckEvent> events, const QString &moreText,
          std::function<void(QDate)> selectDate,
          std::function<void(DuckEvent)> selectEvent, QWidget *parent)
      : QWidget(parent), mDate(date), mAdjacent(adjacent), mEvents(std::move(events)),
        mSelectEvent(std::move(selectEvent)), mMoreText(moreText) {
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    mDateButton = new QToolButton(this);
    mDateButton->setObjectName(QStringLiteral("monthDate"));
    mDateButton->setProperty("calendarDate", date);
    mDateButton->setText(QString::number(date.day()));
    mDateButton->setAccessibleName(QLocale().toString(date, QLocale::LongFormat));
    mDateButton->setToolTip(mDateButton->accessibleName());
    mDateButton->setCheckable(true);
    mDateButton->setChecked(selected);
    mDateButton->setAutoRaise(true);
    connect(mDateButton, &QToolButton::clicked, this, [selectDate, date] { selectDate(date); });
    for (const auto &item : mEvents) {
      auto *button = new EventButton(eventLabel(item), this);
      connect(button, &QPushButton::clicked, this,
              [select = mSelectEvent, item] { select(item); });
      mButtons.append(button);
    }
    mMore = new QToolButton(this);
    mMore->setObjectName(QStringLiteral("monthOverflow"));
    mMore->setProperty("calendarDate", date);
    mMore->setAutoRaise(true);
    connect(mMore, &QToolButton::clicked, this, [this] {
      // Copy each event into its action; no row indexes or borrowed DB objects.
      auto *menu = new QMenu(this);
      menu->setAttribute(Qt::WA_DeleteOnClose);
      for (int i = mVisibleCount; i < mEvents.size(); ++i) {
        const auto item = mEvents.at(i);
        auto *action = menu->addAction(eventLabel(item));
        action->setToolTip(eventLabel(item));
        connect(action, &QAction::triggered, menu, [select = mSelectEvent, item] { select(item); });
      }
      menu->popup(mMore->mapToGlobal(QPoint(0, mMore->height())));
    });
  }

  void setSelected(bool selected) {
    mDateButton->setChecked(selected);
    update();
  }

  QDate date() const { return mDate; }

protected:
  void resizeEvent(QResizeEvent *event) override {
    QWidget::resizeEvent(event);
    const auto rowHeight = std::max(24, fontMetrics().height() + 8);
    const auto dateHeight = std::max(28, fontMetrics().height() + 10);
    mDateButton->setGeometry(4, 3, std::min(width() - 8, 38), dateHeight);
    const auto capacity = std::max(0, (height() - dateHeight - 9) / rowHeight);
    const bool overflow = mEvents.size() > capacity;
    mVisibleCount = overflow ? std::max(0, capacity - 1) : mEvents.size();
    int y = dateHeight + 5;
    for (int i = 0; i < mButtons.size(); ++i) {
      mButtons[i]->setGeometry(3, y, std::max(0, width() - 6), rowHeight);
      mButtons[i]->setVisible(i < mVisibleCount);
      if (i < mVisibleCount) {
        y += rowHeight;
      }
    }
    // Even when no event row fits, keep the overflow control at the bottom.
    mMore->setGeometry(3, std::min(y, std::max(0, height() - rowHeight - 2)),
                       std::max(0, width() - 6), rowHeight);
    mMore->setText(mMoreText.arg(mEvents.size() - mVisibleCount));
    mMore->setAccessibleName(mMore->text());
    mMore->setVisible(overflow);
  }

  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(mAdjacent ? QPalette::AlternateBase : QPalette::Base));
    if (mDateButton->isChecked()) {
      auto tint = palette().color(QPalette::Highlight);
      tint.setAlpha(28);
      painter.fillRect(rect(), tint);
    }
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
  }

private:
  QDate mDate;
  bool mAdjacent;
  QVector<DuckEvent> mEvents;
  std::function<void(DuckEvent)> mSelectEvent;
  QString mMoreText;
  QToolButton *mDateButton;
  QToolButton *mMore;
  QVector<EventButton *> mButtons;
  int mVisibleCount = 0;
};

} // namespace

MonthCalendarWidget::MonthCalendarWidget(QWidget *parent)
    : QWidget(parent), mGrid(new QGridLayout(this)) {
  mGrid->setContentsMargins(0, 0, 0, 0);
  mGrid->setSpacing(0);
  setMonth(QDate::currentDate());
}

void MonthCalendarWidget::setMonth(QDate month) {
  if (!month.isValid()) {
    return;
  }
  mMonth = QDate(month.year(), month.month(), 1);
  rebuild();
}

void MonthCalendarWidget::setSelectedDate(QDate date) {
  mSelectedDate = date;
  for (int i = 0; i < mGrid->count(); ++i) {
    if (auto *cell = dynamic_cast<DayCell *>(mGrid->itemAt(i)->widget())) {
      cell->setSelected(cell->date() == date);
    }
  }
}

void MonthCalendarWidget::setEvents(QVector<DuckEvent> events) {
  mEvents = std::move(events);
  std::stable_sort(mEvents.begin(), mEvents.end(), [](const DuckEvent &left, const DuckEvent &right) {
    return left.start_date.value_or(0) < right.start_date.value_or(0);
  });
  rebuild();
}

QDate MonthCalendarWidget::firstVisibleDate() const { return mFirst; }
QDate MonthCalendarWidget::lastVisibleDate() const { return mLast; }

void MonthCalendarWidget::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LocaleChange || event->type() == QEvent::LanguageChange) {
    rebuild();
  }
}

void MonthCalendarWidget::rebuild() {
  while (auto *item = mGrid->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  const auto locale = QLocale();
  const auto origin = static_cast<int>(locale.firstDayOfWeek());
  const auto offset = (mMonth.dayOfWeek() - origin + 7) % 7;
  mFirst = mMonth.addDays(-offset);
  const auto rows = std::max(5, (offset + mMonth.daysInMonth() + 6) / 7);
  mLast = mFirst.addDays(rows * 7 - 1);
  mGrid->setRowStretch(0, 0);
  for (int row = 1; row <= 6; ++row) {
    mGrid->setRowStretch(row, row <= rows ? 1 : 0);
    mGrid->setRowMinimumHeight(row, row <= rows ? 58 : 0);
  }
  for (int column = 0; column < 7; ++column) {
    const auto weekday = (origin - 1 + column) % 7 + 1;
    auto *label = new QLabel(locale.standaloneDayName(weekday, QLocale::ShortFormat), this);
    label->setAlignment(Qt::AlignCenter);
    label->setFixedHeight(std::max(28, fontMetrics().height() + 10));
    mGrid->addWidget(label, 0, column);
    mGrid->setColumnStretch(column, 1);
  }
  const auto tz = QTimeZone::systemTimeZone();
  for (int i = 0; i < rows * 7; ++i) {
    const auto date = mFirst.addDays(i);
    const auto startMs = QDateTime(date, QTime(0, 0), tz).toMSecsSinceEpoch();
    const auto endMs = QDateTime(date.addDays(1), QTime(0, 0), tz).toMSecsSinceEpoch();
    QVector<DuckEvent> dayEvents;
    for (const auto &event : mEvents) {
      if (event.start_date && event.end_date && *event.start_date < endMs && *event.end_date > startMs) {
        dayEvents.append(event);
      }
    }
    auto *cell = new DayCell(date, date.month() != mMonth.month(), date == mSelectedDate,
                             std::move(dayEvents), tr("+%1 more"),
                             [this](QDate selected) {
                               setSelectedDate(selected);
                               emit dateSelected(selected);
                             },
                             [this, date](DuckEvent event) {
                               setSelectedDate(date);
                               emit eventSelected(event);
                             }, this);
    mGrid->addWidget(cell, i / 7 + 1, i % 7);
  }
}
