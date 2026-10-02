#include "rounded_calendar_widget.h"

#include "constants.hpp"

#include <QPainter>
#include <QLocale>

namespace {
constexpr int kOuterMargin = 10;
constexpr int kHeaderHeight = 28;
constexpr int kHeaderGap = 10;
constexpr int kHeaderSectionHeight = 22;
constexpr int kDayNameHeight = 18;
constexpr int kGridRows = 6;
constexpr int kGridColumns = 7;
constexpr qreal kCardRadius = 16.0;
constexpr qreal kCellRadius = 8.0;
constexpr int kNavButtonWidth = 24;

QString monthYearLabelText(const QDate& shownMonth) {
  const auto locale = QLocale::system();
  return QStringLiteral("%1 %2")
    .arg(locale.standaloneMonthName(shownMonth.month(), QLocale::LongFormat))
    .arg(shownMonth.year());
}

QString weekdayLabel(const int dayOfWeek) {
  const auto locale = QLocale::system();
  return locale.standaloneDayName(dayOfWeek, QLocale::ShortFormat).left(2).toLower();
}
} // namespace

RoundedCalendarWidget::RoundedCalendarWidget(QWidget *parent)
    : QWidget(parent), mSelectedDate(QDate::currentDate()),
      mShownMonth(QDate(QDate::currentDate().year(), QDate::currentDate().month(), 1)) {
  setObjectName("roundedCalendarCard");
  setAttribute(Qt::WA_StyledBackground, false);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setMinimumHeight(262);

  mPreviousMonthButton = new QToolButton(this);
  mPreviousMonthButton->setText(QStringLiteral("‹"));
  mPreviousMonthButton->setAutoRaise(true);
  mPreviousMonthButton->setCursor(Qt::PointingHandCursor);

  mNextMonthButton = new QToolButton(this);
  mNextMonthButton->setText(QStringLiteral("›"));
  mNextMonthButton->setAutoRaise(true);
  mNextMonthButton->setCursor(Qt::PointingHandCursor);

  mMonthYearLabel = new QLabel(this);
  mMonthYearLabel->setAlignment(Qt::AlignCenter);

  setStyleSheet(
      "QToolButton {"
      " background: transparent;"
      " color: palette(text);"
      " border: none;"
      " padding: 2px 6px;"
      " font-size: 18px;"
      "}"
      "QToolButton:hover {"
      " background: rgba(255, 255, 255, 0.08);"
      " border-radius: 8px;"
      "}"
      "QLabel {"
      " color: palette(text);"
      " font-weight: 600;"
      "}");

  syncNavigationControls();

  connect(mPreviousMonthButton, &QToolButton::clicked, this, [this]() {
    mShownMonth = mShownMonth.addMonths(-1);
    syncNavigationControls();
    update();
  });
  connect(mNextMonthButton, &QToolButton::clicked, this, [this]() {
    mShownMonth = mShownMonth.addMonths(1);
    syncNavigationControls();
    update();
  });
}

void RoundedCalendarWidget::setSelectedDate(const QDate &date) {
  if (!date.isValid()) {
    return;
  }

  mSelectedDate = date;
  mShownMonth = QDate(date.year(), date.month(), 1);
  syncNavigationControls();
  update();
}

QDate RoundedCalendarWidget::selectedDate() const { return mSelectedDate; }

void RoundedCalendarWidget::setDateTextFormat(const QDate &date,
                                              const QTextCharFormat &format) {
  if (!date.isValid()) {
    return;
  }

  mDateFormats.insert(date, format);
  update();
}

void RoundedCalendarWidget::mousePressEvent(QMouseEvent *event) {
  if (!event || event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }

  const auto rect = gridRect();
  if (!rect.contains(event->position().toPoint())) {
    QWidget::mousePressEvent(event);
    return;
  }

  const auto colWidth = rect.width() / kGridColumns;
  const auto rowHeight = rect.height() / kGridRows;
  if (colWidth <= 0 || rowHeight <= 0) {
    QWidget::mousePressEvent(event);
    return;
  }

  const auto localX = event->position().toPoint().x() - rect.left();
  const auto localY = event->position().toPoint().y() - rect.top();
  const auto column = localX / colWidth;
  const auto row = localY / rowHeight;
  if (column < 0 || column >= kGridColumns || row < 0 || row >= kGridRows) {
    QWidget::mousePressEvent(event);
    return;
  }

  const auto date = firstVisibleDate().addDays(row * kGridColumns + column);
  setSelectedDate(date);
  emit clicked(date);
  event->accept();
}

void RoundedCalendarWidget::paintEvent(QPaintEvent *event) {
  QWidget::paintEvent(event);

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  painter.setPen(Qt::NoPen);
  painter.setBrush(pcm::widgets::constants::kCalendarCardBackgroundColor);
  painter.drawRoundedRect(rect(), kCardRadius, kCardRadius);

  const QRect namesRect(kOuterMargin,
                        kOuterMargin + kHeaderSectionHeight + kHeaderGap,
                        width() - 2 * kOuterMargin, kDayNameHeight);
  const auto dayColumnWidth = namesRect.width() / kGridColumns;

  QFont dayNameFont = font();
  dayNameFont.setPointSizeF(dayNameFont.pointSizeF() - 0.5);
  painter.setFont(dayNameFont);

  for (int day = 0; day < kGridColumns; ++day) {
    const int dayOfWeek = day + 1;
    const QRect dayRect(namesRect.left() + day * dayColumnWidth, namesRect.top(),
                        dayColumnWidth, namesRect.height());
    const QColor dayColor =
        dayOfWeek >= 6 ? QColor(255, 0, 0) : palette().color(QPalette::Text);
    painter.setPen(dayColor);
    painter.drawText(dayRect, Qt::AlignCenter, weekdayLabel(dayOfWeek));
  }

  const auto startDate = firstVisibleDate();
  const auto today = QDate::currentDate();

  for (int row = 0; row < kGridRows; ++row) {
    for (int column = 0; column < kGridColumns; ++column) {
      const auto date = startDate.addDays(row * kGridColumns + column);
      const QRect rect = cellRect(row, column);
      const bool isCurrentMonth = date.month() == mShownMonth.month()
                                  && date.year() == mShownMonth.year();
      const bool isSelected = date == mSelectedDate;

      if (isSelected) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(93, 123, 230));
        painter.drawRoundedRect(rect.adjusted(6, 4, -6, -4), kCellRadius, kCellRadius);
      }

      QColor textColor =
          isCurrentMonth ? palette().color(QPalette::Text) : QColor(255, 255, 255, 70);
      if (date.dayOfWeek() >= 6 && isCurrentMonth) {
        textColor = QColor(255, 0, 0);
      }

      if (const auto it = mDateFormats.constFind(date); it != mDateFormats.constEnd()) {
        if (it->foreground().style() != Qt::NoBrush) {
          textColor = it->foreground().color();
        }
      }

      painter.setPen(textColor);
      QFont cellFont = font();
      cellFont.setBold(isSelected);
      if (date == today) {
        cellFont.setWeight(QFont::DemiBold);
      }
      painter.setFont(cellFont);
      painter.drawText(rect, Qt::AlignCenter, QString::number(date.day()));

      if (const auto it = mDateFormats.constFind(date); it != mDateFormats.constEnd()) {
        if (it->underlineStyle() != QTextCharFormat::NoUnderline) {
          painter.setPen(QPen(it->underlineColor(), 1.5));
          const int underlineY = rect.bottom() - 6;
          painter.drawLine(rect.left() + 12, underlineY, rect.right() - 12, underlineY);
        }
      }
    }
  }
}

void RoundedCalendarWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);

  const int contentWidth = width() - 2 * kOuterMargin;
  mPreviousMonthButton->setGeometry(kOuterMargin, kOuterMargin, kNavButtonWidth,
                                    kHeaderHeight);
  mNextMonthButton->setGeometry(width() - kOuterMargin - kNavButtonWidth, kOuterMargin,
                                kNavButtonWidth, kHeaderHeight);
  mMonthYearLabel->setGeometry(kOuterMargin + kNavButtonWidth + 12, kOuterMargin,
                               contentWidth - 2 * (kNavButtonWidth + 12), kHeaderHeight);
}

void RoundedCalendarWidget::syncNavigationControls() const {
  if (!mMonthYearLabel) {
    return;
  }

  mMonthYearLabel->setText(monthYearLabelText(mShownMonth));
}

QRect RoundedCalendarWidget::gridRect() const {
  const int top = kOuterMargin + kHeaderSectionHeight + kHeaderGap + kDayNameHeight + 4;
  return QRect(kOuterMargin, top, width() - 2 * kOuterMargin,
               height() - top - kOuterMargin);
}

QRect RoundedCalendarWidget::cellRect(const int row, const int column) const {
  const auto rect = gridRect();
  const int colWidth = rect.width() / kGridColumns;
  const int rowHeight = rect.height() / kGridRows;
  return QRect(rect.left() + column * colWidth, rect.top() + row * rowHeight, colWidth,
               rowHeight);
}

QDate RoundedCalendarWidget::firstVisibleDate() const {
  const auto firstOfMonth = QDate(mShownMonth.year(), mShownMonth.month(), 1);
  const int daysFromMonday = firstOfMonth.dayOfWeek() - 1;
  return firstOfMonth.addDays(-daysFromMonday);
}
