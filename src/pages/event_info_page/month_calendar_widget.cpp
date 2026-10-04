#include "month_calendar_widget.h"

#include "app_settings.h"

#include <QApplication>
#include <QEvent>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QStyleOptionButton>
#include <QResizeEvent>
#include <QTimeZone>
#include <QToolButton>
#include <algorithm>
#include <functional>
#include <tuple>

namespace {

QString eventTime(const DuckEvent &event) {
  return QDateTime::fromMSecsSinceEpoch(event.start_date.value_or(0), QTimeZone::systemTimeZone())
      .toString(QStringLiteral("HH:mm"));
}

QString eventTitle(const DuckEvent &event) {
  auto title = QString::fromStdString(event.name.value_or(""));
  const auto client = QString::fromStdString(event.client_name.value_or(""));
  if (!client.isEmpty() && client != title) {
    title += QStringLiteral(" · ") + client;
  }
  return title;
}

QString eventLabel(const DuckEvent &event) {
  return eventTime(event) + QStringLiteral("  ") + eventTitle(event);
}

// Layout metrics (device independent pixels).
constexpr int kMinRowHeight = 112;
constexpr int kChipHeight = 38; // two lines: time, then title
constexpr int kMoreHeight = 18;
constexpr int kChipGap = 3;
constexpr int kCellPadding = 6;

QColor withAlpha(QColor color, const int alpha) {
  color.setAlpha(alpha);
  return color;
}

// Weekday header: muted text drawn from the live palette, so a theme switch
// needs no restyling (no style sheets on ancestors of Qlementine widgets).
class WeekdayLabel final : public QLabel {
public:
  using QLabel::QLabel;

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    auto f = font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1.0));
    painter.setFont(f);
    painter.setPen(withAlpha(palette().color(QPalette::Text), 140));
    // Left edge matches the day numbers inside the cells below.
    painter.drawText(rect().adjusted(kCellPadding + 6, 0, 0, 0),
                     Qt::AlignLeft | Qt::AlignVCenter, text());
  }
};

bool sameEventData(const DuckEvent &a, const DuckEvent &b) {
  return a.id == b.id && a.name == b.name && a.description == b.description &&
         a.client_name == b.client_name && a.is_work_event == b.is_work_event &&
         a.event_stat_id == b.event_stat_id && a.payment_stat_id == b.payment_stat_id &&
         a.start_date == b.start_date && a.end_date == b.end_date &&
         a.duration == b.duration && a.cost == b.cost && a.is_online == b.is_online &&
         a.meeting_url == b.meeting_url && a.series_id == b.series_id &&
         a.original_occurrence_start == b.original_occurrence_start &&
         a.cancellation_reason == b.cancellation_reason && a.canceled_by == b.canceled_by &&
         a.buffer_before_minutes == b.buffer_before_minutes &&
         a.buffer_after_minutes == b.buffer_after_minutes &&
         a.is_virtual_occurrence == b.is_virtual_occurrence &&
         a.provider_kind == b.provider_kind && a.meeting_ref == b.meeting_ref &&
         a.invitation_state == b.invitation_state;
}

void sortEvents(QVector<DuckEvent> &events) {
  std::stable_sort(events.begin(), events.end(), [](const DuckEvent &left, const DuckEvent &right) {
    return left.start_date.value_or(0) < right.start_date.value_or(0);
  });
}

bool sameEvents(const QVector<DuckEvent> &left, const QVector<DuckEvent> &right) {
  return std::equal(left.cbegin(), left.cend(), right.cbegin(), right.cend(), sameEventData);
}

// Stable identity of an occurrence across rebuilds (range-local ids are not).
QString eventKey(const DuckEvent &event) {
  if (event.series_id && event.original_occurrence_start) {
    return QStringLiteral("s%1@%2").arg(*event.series_id).arg(*event.original_occurrence_start);
  }
  return QStringLiteral("i%1").arg(event.id);
}

// Rounded chip: leading dot, small muted time, then the (elidable) title.
// Work/personal colors come from the same settings as the day timeline
// (fill = color, border = color.darker(165)); text contrasts with the fill.
class EventButton final : public QPushButton {
public:
  EventButton(const DuckEvent &event, const bool adjacent, QWidget *parent)
      : QPushButton(parent), mTime(eventTime(event)), mTitle(eventTitle(event)),
        mWork(event.is_work_event),
        mAdjacent(adjacent) {
    const auto label = eventLabel(event);
    setObjectName(QStringLiteral("monthEvent"));
    setToolTip(label);
    setAccessibleName(label);
    setFlat(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    syncColors();
  }

  // Re-reads the configured colors; also published as properties for tests.
  void syncColors() {
    mFill = mWork ? pcm::app_settings::workEventColor() : pcm::app_settings::personalEventColor();
    mFill.setAlpha(255);
    mBorder = mFill.darker(165);
    // Perceived luminance picks light or dark text.
    const double luma = 0.299 * mFill.red() + 0.587 * mFill.green() + 0.114 * mFill.blue();
    mText = luma > 150 ? QColor(20, 22, 28) : QColor(255, 255, 255);
    setProperty("chipFill", mFill);
    setProperty("chipBorder", mBorder);
    setProperty("chipText", mText);
    update();
  }

protected:
  void resizeEvent(QResizeEvent *event) override {
    QPushButton::resizeEvent(event);
    setText(fontMetrics().elidedText(mTime + QStringLiteral("  ") + mTitle, Qt::ElideRight,
                                     std::max(0, width() - 12)));
  }

  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (mAdjacent) {
      painter.setOpacity(0.55);
    }
    const auto text = mText;
    const bool hot = underMouse() || hasFocus();
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(mBorder, 1));
    painter.setBrush(hot ? mFill.lighter(112) : mFill);
    painter.drawRoundedRect(box, 6, 6);
    if (hasFocus()) {
      painter.setPen(QPen(text, 1.5));
      painter.setBrush(Qt::NoBrush);
      painter.drawRoundedRect(box, 6, 6);
    }
    constexpr qreal dot = 6;
    painter.setPen(Qt::NoPen);
    painter.setBrush(withAlpha(text, 210));
    auto small = font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.0));
    const QFontMetrics smallMetrics(small);
    const int line1 = 4;
    const int line1Height = smallMetrics.height();
    const bool showDot = width() >= 72; // very narrow cells keep the text instead
    if (showDot) {
      painter.drawEllipse(QRectF(8, line1 + (line1Height - dot) / 2, dot, dot));
    }
    painter.setFont(small);
    painter.setPen(withAlpha(text, 200));
    const int timeX = showDot ? 8 + static_cast<int>(dot) + 5 : 8;
    const int timeWidth = std::max(0, width() - timeX - 6);
    painter.drawText(QRect(timeX, line1, timeWidth, line1Height), Qt::AlignVCenter | Qt::AlignLeft,
                     smallMetrics.elidedText(mTime, Qt::ElideRight, timeWidth));
    painter.setFont(font());
    painter.setPen(text);
    const auto titleWidth = std::max(0, width() - 16);
    painter.drawText(QRect(8, line1 + line1Height, titleWidth, height() - line1 - line1Height - 2),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     fontMetrics().elidedText(mTitle, Qt::ElideRight, titleWidth));
  }

private:
  QString mTime;
  QString mTitle;
  bool mWork;
  bool mAdjacent;
  QColor mFill;
  QColor mBorder;
  QColor mText;
};

// Day number. The text is painted explicitly: a checked QToolButton hides its
// label under Qlementine, which left the selected day as an empty badge.
class DateButton final : public QToolButton {
public:
  DateButton(const bool adjacent, const bool today, QWidget *parent)
      : QToolButton(parent), mAdjacent(adjacent), mToday(today) {
    setCursor(Qt::PointingHandCursor);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto pal = palette();
    const qreal d = std::min(width(), height()) - 2;
    const QRectF circle((width() - d) / 2, (height() - d) / 2, d, d);
    QColor textColor = pal.color(QPalette::Text);
    if (isChecked()) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(pal.color(QPalette::Highlight));
      painter.drawEllipse(circle);
      textColor = pal.color(QPalette::HighlightedText);
    } else if (mToday) {
      painter.setPen(QPen(pal.color(QPalette::Highlight), 1.5));
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(circle.adjusted(0.75, 0.75, -0.75, -0.75));
    } else if (underMouse()) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(withAlpha(pal.color(QPalette::Text), 30));
      painter.drawEllipse(circle);
    }
    if (hasFocus() && !isChecked()) {
      painter.setPen(QPen(pal.color(QPalette::Highlight), 1.5, Qt::DotLine));
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(circle.adjusted(-1, -1, 1, 1));
    }
    if (mAdjacent && !isChecked()) {
      textColor.setAlpha(100);
    }
    auto f = font();
    f.setBold(isChecked() || mToday);
    painter.setFont(f);
    painter.setPen(textColor);
    painter.drawText(rect(), Qt::AlignCenter, text());
  }

private:
  bool mAdjacent;
  bool mToday;
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
    mDateButton = new DateButton(adjacent, date == QDate::currentDate(), this);
    mDateButton->setObjectName(QStringLiteral("monthDate"));
    mDateButton->setProperty("calendarDate", date);
    mDateButton->setText(QString::number(date.day()));
    mDateButton->setAccessibleName(QLocale().toString(date, QLocale::LongFormat));
    mDateButton->setToolTip(mDateButton->accessibleName());
    mDateButton->setCheckable(true);
    mDateButton->setChecked(selected);
    connect(mDateButton, &QToolButton::clicked, this, [selectDate, date] { selectDate(date); });
    for (const auto &item : mEvents) {
      auto *button = new EventButton(item, mAdjacent, this);
      button->setProperty("eventKey", eventKey(item));
      connect(button, &QPushButton::clicked, this,
              [select = mSelectEvent, item] { select(item); });
      mButtons.append(button);
    }
    mMore = new QToolButton(this);
    mMore->setObjectName(QStringLiteral("monthOverflow"));
    mMore->setProperty("calendarDate", date);
    mMore->setAutoRaise(true);
    mMore->setCursor(Qt::PointingHandCursor);
    auto moreFont = mMore->font();
    moreFont.setPointSizeF(std::max(7.0, moreFont.pointSizeF() - 1.0));
    mMore->setFont(moreFont);
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

  // Hairline edges: every cell closes itself on the bottom; the right edge is
  // omitted on the last column and the top edge is drawn on the first row.
  void setEdges(const bool top, const bool right) {
    mTopEdge = top;
    mRightEdge = right;
  }

  QDate date() const { return mDate; }
  QWidget *dateButton() const { return mDateButton; }
  QWidget *overflowButton() const { return mMore; }
  QWidget *eventButton(const QString &key) const {
    for (auto *button : mButtons) {
      if (button->property("eventKey").toString() == key) {
        return button;
      }
    }
    return nullptr;
  }

protected:
  void resizeEvent(QResizeEvent *event) override {
    QWidget::resizeEvent(event);
    const auto dateSize = std::max(26, fontMetrics().height() + 8);
    mDateButton->setGeometry(kCellPadding, kCellPadding - 1, dateSize, dateSize);
    const auto firstChipY = kCellPadding + dateSize + 3;
    const auto step = kChipHeight + kChipGap;
    const auto available = height() - firstChipY - kCellPadding + kChipGap;
    const bool overflow = mEvents.size() * step > available;
    mVisibleCount = overflow ? std::max(0, (available - kMoreHeight) / step) : mEvents.size();
    int y = firstChipY;
    for (int i = 0; i < mButtons.size(); ++i) {
      mButtons[i]->setGeometry(kCellPadding, y, std::max(0, width() - 2 * kCellPadding), kChipHeight);
      mButtons[i]->setVisible(i < mVisibleCount);
      if (i < mVisibleCount) {
        y += step;
      }
    }
    // Even when no event row fits, keep the overflow control at the bottom.
    mMore->setGeometry(kCellPadding, std::min(y, std::max(0, height() - kMoreHeight - 2)),
                       std::max(0, width() - 2 * kCellPadding), kMoreHeight);
    mMore->setText(width() < 90 ? QStringLiteral("+%1").arg(mEvents.size() - mVisibleCount)
                                : mMoreText.arg(mEvents.size() - mVisibleCount));
    mMore->setAccessibleName(mMoreText.arg(mEvents.size() - mVisibleCount));
    mMore->setVisible(overflow);
  }

  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    const auto pal = palette();
    if (mDateButton->isChecked()) {
      painter.fillRect(rect(), withAlpha(pal.color(QPalette::Highlight), 30));
    }
    painter.setPen(withAlpha(pal.color(QPalette::Text), 38));
    painter.drawLine(0, height() - 1, width(), height() - 1);
    if (mTopEdge) {
      painter.drawLine(0, 0, width(), 0);
    }
    if (mRightEdge) {
      painter.drawLine(width() - 1, 0, width() - 1, height());
    }
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
  bool mTopEdge = false;
  bool mRightEdge = true;
};

} // namespace

MonthCalendarWidget::MonthCalendarWidget(QWidget *parent)
    : QWidget(parent), mGrid(new QGridLayout(this)) {
  mGrid->setContentsMargins(0, 0, 0, 0);
  mGrid->setSpacing(0);
  const auto today = QDate::currentDate();
  mMonth = QDate(today.year(), today.month(), 1);
  rebuild();
}

void MonthCalendarWidget::setMonth(QDate month) {
  if (!month.isValid()) {
    return;
  }
  const auto first = QDate(month.year(), month.month(), 1);
  if (first == mMonth) {
    return;
  }
  mMonth = first;
  rebuild();
}

void MonthCalendarWidget::setMonthAndEvents(QDate month, QVector<DuckEvent> events) {
  if (!month.isValid()) {
    return;
  }
  const auto first = QDate(month.year(), month.month(), 1);
  sortEvents(events);
  if (first == mMonth && sameEvents(events, mEvents)) {
    return;
  }
  mMonth = first;
  mEvents = std::move(events);
  rebuild();
}

std::pair<QDate, QDate> MonthCalendarWidget::visibleRange(QDate month) {
  const auto first = QDate(month.year(), month.month(), 1);
  const auto origin = static_cast<int>(QLocale().firstDayOfWeek());
  const auto offset = (first.dayOfWeek() - origin + 7) % 7;
  const auto start = first.addDays(-offset);
  const auto rows = std::max(5, (offset + first.daysInMonth() + 6) / 7);
  return {start, start.addDays(rows * 7 - 1)};
}

int MonthCalendarWidget::rebuildCount() const { return mRebuildCount; }

void MonthCalendarWidget::refreshAppearance() {
  for (auto *button : findChildren<QPushButton *>(QStringLiteral("monthEvent"))) {
    static_cast<EventButton *>(button)->syncColors();
  }
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
  sortEvents(events);
  if (sameEvents(events, mEvents)) {
    return;
  }
  mEvents = std::move(events);
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
  ++mRebuildCount;
  std::tie(mFirst, mLast) = visibleRange(mMonth);
  // Remember what keyboard focus was on so it can follow the rebuilt cell.
  enum class FocusKind { None, Date, Event, Overflow } kind = FocusKind::None;
  QDate focusDate;
  QString focusEvent;
  if (auto *focused = QApplication::focusWidget(); focused && isAncestorOf(focused)) {
    for (QWidget *w = focused; w && w != this; w = w->parentWidget()) {
      if (auto *cell = dynamic_cast<DayCell *>(w)) {
        focusDate = cell->date();
        if (focused == cell->dateButton()) {
          kind = FocusKind::Date;
        } else if (focused == cell->overflowButton()) {
          kind = FocusKind::Overflow;
        } else {
          kind = FocusKind::Event;
          focusEvent = focused->property("eventKey").toString();
        }
        break;
      }
    }
  }
  while (auto *item = mGrid->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  const auto locale = QLocale();
  const auto origin = static_cast<int>(locale.firstDayOfWeek());
  const auto rows = static_cast<int>(mFirst.daysTo(mLast) + 1) / 7;
  mGrid->setRowStretch(0, 0);
  for (int row = 1; row <= 6; ++row) {
    mGrid->setRowStretch(row, row <= rows ? 1 : 0);
    mGrid->setRowMinimumHeight(row, row <= rows ? kMinRowHeight : 0);
  }
  const auto headerHeight = std::max(28, fontMetrics().height() + 10);
  // Rows share all extra height equally, so the grid's bottom edge always meets
  // the bottom of the area it lives in (and the left column beside it).
  setMinimumHeight(headerHeight + rows * kMinRowHeight);
  setMaximumHeight(QWIDGETSIZE_MAX);
  for (int column = 0; column < 7; ++column) {
    const auto weekday = (origin - 1 + column) % 7 + 1;
    auto *label = new WeekdayLabel(locale.standaloneDayName(weekday, QLocale::ShortFormat), this);
    label->setAlignment(Qt::AlignCenter);
    label->setFixedHeight(headerHeight);
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
    cell->setEdges(i < 7, i % 7 != 6);
    mGrid->addWidget(cell, i / 7 + 1, i % 7);
  }
  if (kind != FocusKind::None) {
    for (int i = 0; i < mGrid->count(); ++i) {
      auto *cell = dynamic_cast<DayCell *>(mGrid->itemAt(i)->widget());
      if (!cell || cell->date() != focusDate) {
        continue;
      }
      QWidget *target = kind == FocusKind::Event   ? cell->eventButton(focusEvent)
                        : kind == FocusKind::Overflow ? cell->overflowButton()
                                                      : cell->dateButton();
      // Event/overflow rows may be laid out differently now; the date is stable.
      (target ? target : cell->dateButton())->setFocus(Qt::OtherFocusReason);
      break;
    }
  }
}
