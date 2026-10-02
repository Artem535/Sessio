#include "month_picker_widget.h"

#include "../../widgets/constants.hpp"

#include <QEvent>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QToolButton>

namespace {
constexpr int kOuterMargin = 10;
constexpr int kHeaderHeight = 28;
constexpr int kHeaderGap = 14;
constexpr int kNavButtonWidth = 24;
constexpr int kTileGap = 8;
constexpr int kColumns = 3;
constexpr int kRows = 4;
constexpr qreal kCardRadius = 16.0;
constexpr qreal kTileRadius = 8.0;

// Painted tile. A real QToolButton keeps keyboard focus and accessibility.
class MonthTile final : public QToolButton {
public:
  MonthTile(const int month, QWidget *parent) : QToolButton(parent), mMonth(month) {
    setObjectName(QStringLiteral("monthPickerTile"));
    setProperty("calendarMonth", month);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
  }
  void setState(const bool displayed, const bool current) {
    mDisplayed = displayed;
    mCurrent = current;
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(Qt::NoPen);
    if (mDisplayed) {
      painter.setBrush(QColor(93, 123, 230));
      painter.drawRoundedRect(box, kTileRadius, kTileRadius);
    } else if (underMouse()) {
      painter.setBrush(QColor(255, 255, 255, 22));
      painter.drawRoundedRect(box, kTileRadius, kTileRadius);
    }
    if (hasFocus()) {
      painter.setPen(QPen(palette().color(QPalette::Highlight), 1.5));
      painter.setBrush(Qt::NoBrush);
      painter.drawRoundedRect(box, kTileRadius, kTileRadius);
    }
    auto f = font();
    f.setBold(mDisplayed);
    if (mCurrent && !mDisplayed) {
      f.setWeight(QFont::DemiBold);
    }
    painter.setFont(f);
    painter.setPen(mDisplayed ? QColor(Qt::white) : mCurrent
                       ? pcm::widgets::constants::kCalendarCurrentDayForegroundColor
                       : palette().color(QPalette::Text));
    painter.drawText(rect(), Qt::AlignCenter, text());
    if (mCurrent) {
      painter.setPen(QPen(pcm::widgets::constants::kCalendarCurrentDayUnderlineColor, 1.5));
      painter.drawLine(width() / 2 - 14, height() - 8, width() / 2 + 14, height() - 8);
    }
  }

private:
  int mMonth;
  bool mDisplayed = false;
  bool mCurrent = false;
};

// Plain painted arrow; Qlementine's tool button hides the label of flat buttons.
class NavButton final : public QToolButton {
public:
  explicit NavButton(QWidget *parent) : QToolButton(parent) {}

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (underMouse()) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(255, 255, 255, 22));
      painter.drawRoundedRect(rect(), 8, 8);
    }
    if (hasFocus()) {
      painter.setPen(QPen(palette().color(QPalette::Highlight), 1.5));
      painter.setBrush(Qt::NoBrush);
      painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    }
    auto f = font();
    f.setPointSize(f.pointSize() + 4);
    painter.setFont(f);
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(rect(), Qt::AlignCenter, text());
  }
};
} // namespace

MonthPickerWidget::MonthPickerWidget(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("monthPickerCard"));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setMinimumHeight(262);
  const auto today = QDate::currentDate();
  mDisplayed = QDate(today.year(), today.month(), 1);
  mYear = mDisplayed.year();

  mPrevious = new NavButton(this);
  mPrevious->setObjectName(QStringLiteral("monthPickerPreviousYear"));
  mPrevious->setText(QStringLiteral("‹"));
  mPrevious->setAccessibleName(tr("Previous year"));
  mNext = new NavButton(this);
  mNext->setObjectName(QStringLiteral("monthPickerNextYear"));
  mNext->setText(QStringLiteral("›"));
  mNext->setAccessibleName(tr("Next year"));
  for (auto *button : {mPrevious, mNext}) {
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
  }
  mYearLabel = new QLabel(this);
  mYearLabel->setObjectName(QStringLiteral("monthPickerYear"));
  mYearLabel->setAlignment(Qt::AlignCenter);
  auto yearFont = mYearLabel->font();
  yearFont.setWeight(QFont::DemiBold);
  mYearLabel->setFont(yearFont);
  for (int month = 1; month <= 12; ++month) {
    auto *tile = new MonthTile(month, this);
    mTiles[month - 1] = tile;
    connect(tile, &QToolButton::clicked, this, [this, month] {
      emit monthSelected(QDate(mYear, month, 1));
    });
  }
  connect(mPrevious, &QToolButton::clicked, this, [this] { setShownYear(mYear - 1); });
  connect(mNext, &QToolButton::clicked, this, [this] { setShownYear(mYear + 1); });
  refreshLabels();
}

void MonthPickerWidget::setDisplayedMonth(const QDate month) {
  if (!month.isValid()) {
    return;
  }
  mDisplayed = QDate(month.year(), month.month(), 1);
  mYear = mDisplayed.year();
  refreshLabels();
}

QDate MonthPickerWidget::displayedMonth() const { return mDisplayed; }
int MonthPickerWidget::shownYear() const { return mYear; }

void MonthPickerWidget::setShownYear(const int year) {
  mYear = year;
  refreshLabels();
}

void MonthPickerWidget::refreshLabels() {
  mYearLabel->setText(QString::number(mYear));
  const auto locale = QLocale();
  const auto today = QDate::currentDate();
  for (int month = 1; month <= 12; ++month) {
    auto *tile = static_cast<MonthTile *>(mTiles[month - 1]);
    const auto name = locale.standaloneMonthName(month, QLocale::ShortFormat);
    tile->setText(name);
    tile->setAccessibleName(locale.standaloneMonthName(month, QLocale::LongFormat) +
                            QLatin1Char(' ') + QString::number(mYear));
    tile->setCheckable(false);
    tile->setProperty("displayed", mDisplayed.year() == mYear && mDisplayed.month() == month);
    tile->setState(mDisplayed.year() == mYear && mDisplayed.month() == month,
                   today.year() == mYear && today.month() == month);
  }
  update();
}

void MonthPickerWidget::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange || event->type() == QEvent::LocaleChange) {
    refreshLabels();
  }
}

void MonthPickerWidget::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);
  painter.setBrush(pcm::widgets::constants::kCalendarCardBackgroundColor);
  painter.drawRoundedRect(rect(), kCardRadius, kCardRadius);
}

void MonthPickerWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  relayout();
}

void MonthPickerWidget::relayout() {
  mPrevious->setGeometry(kOuterMargin, kOuterMargin, kNavButtonWidth, kHeaderHeight);
  mNext->setGeometry(width() - kOuterMargin - kNavButtonWidth, kOuterMargin, kNavButtonWidth,
                     kHeaderHeight);
  mYearLabel->setGeometry(kOuterMargin + kNavButtonWidth + 12, kOuterMargin,
                          width() - 2 * (kOuterMargin + kNavButtonWidth + 12), kHeaderHeight);
  const int top = kOuterMargin + kHeaderHeight + kHeaderGap;
  const int gridWidth = width() - 2 * kOuterMargin;
  const int gridHeight = height() - top - kOuterMargin;
  const int tileWidth = (gridWidth - (kColumns - 1) * kTileGap) / kColumns;
  const int tileHeight = (gridHeight - (kRows - 1) * kTileGap) / kRows;
  for (int i = 0; i < 12; ++i) {
    mTiles[i]->setGeometry(kOuterMargin + (i % kColumns) * (tileWidth + kTileGap),
                           top + (i / kColumns) * (tileHeight + kTileGap), tileWidth, tileHeight);
  }
}
