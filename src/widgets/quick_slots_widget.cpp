#include "quick_slots_widget.h"

#include "app_settings.h"

#include <QDateTime>
#include <QFont>
#include <QPushButton>
#include <QTimeZone>
#include <QVBoxLayout>

QuickSlotsWidget::QuickSlotsWidget(QWidget *parent)
    : QWidget(parent), mSelectedDate(QDate::currentDate()) {
  auto *rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(8);

  mTitleLabel = new QLabel(tr("Quick session slots"), this);
  QFont titleFont = mTitleLabel->font();
  titleFont.setBold(true);
  mTitleLabel->setFont(titleFont);
  rootLayout->addWidget(mTitleLabel);

  mSlotsContainer = new QWidget(this);
  mSlotsLayout = new QGridLayout(mSlotsContainer);
  mSlotsLayout->setContentsMargins(0, 0, 0, 0);
  mSlotsLayout->setHorizontalSpacing(8);
  mSlotsLayout->setVerticalSpacing(8);
  rootLayout->addWidget(mSlotsContainer);

  mEmptyLabel = new QLabel(tr("No quick slots available for the selected day."), this);
  mEmptyLabel->setWordWrap(true);
  mEmptyLabel->setStyleSheet("color: rgba(255, 255, 255, 0.60);");
  rootLayout->addWidget(mEmptyLabel);

  refreshSlots();
}

void QuickSlotsWidget::setSelectedDate(const QDate &date) {
  if (mSelectedDate == date) {
    return;
  }

  mSelectedDate = date;
  refreshSlots();
}

void QuickSlotsWidget::setBusyIntervals(
    const QVector<QPair<QDateTime, QDateTime>> &intervals) {
  mBusyIntervals = intervals;
  refreshSlots();
}

void QuickSlotsWidget::refreshSlots() {
  clearSlots();

  const auto workDayStart = pcm::app_settings::workDayStart();
  const auto workDayEnd = pcm::app_settings::workDayEnd();
  const auto durationMinutes = pcm::app_settings::defaultSessionDurationMinutes();
  const auto hideOverlaps = pcm::app_settings::preventEventOverlaps();

  if (!mSelectedDate.isValid() || !workDayStart.isValid() || !workDayEnd.isValid() ||
      workDayStart >= workDayEnd || durationMinutes <= 0) {
    mSlotsContainer->setVisible(false);
    mEmptyLabel->setText(tr("Quick slots are not configured yet."));
    mEmptyLabel->setVisible(true);
    return;
  }

  int visibleSlotCount = 0;
  int row = 0;
  int column = 0;
  constexpr int kColumns = 4;

  for (QTime slotTime = workDayStart;
       slotTime.addSecs(durationMinutes * 60) <= workDayEnd;
       slotTime = slotTime.addSecs(durationMinutes * 60)) {
    const auto slotStart =
        QDateTime(mSelectedDate, slotTime, QTimeZone(QTimeZone::LocalTime));
    const auto slotEnd = slotStart.addSecs(durationMinutes * 60);

    if (hideOverlaps && slotOverlapsAnyEvent(slotStart, slotEnd)) {
      continue;
    }

    auto *button = new QPushButton(slotTime.toString("HH:mm"), mSlotsContainer);
    button->setMinimumHeight(32);
    button->setCursor(Qt::PointingHandCursor);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setStyleSheet(
        "QPushButton {"
        "  padding: 6px 14px;"
        "  border-radius: 16px;"
        "  border: 1px solid rgba(255, 255, 255, 0.14);"
        "  background-color: rgba(255, 255, 255, 0.05);"
        "  color: rgba(255, 255, 255, 0.92);"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover {"
        "  border-color: rgba(255, 255, 255, 0.22);"
        "  background-color: rgba(255, 255, 255, 0.09);"
        "}"
        "QPushButton:pressed {"
        "  background-color: rgba(255, 255, 255, 0.14);"
        "}");
    connect(button, &QPushButton::clicked, this,
            [this, slotTime, durationMinutes]() {
              emit quickSlotSelected(slotTime, durationMinutes);
            });

    mSlotsLayout->addWidget(button, row, column);
    ++visibleSlotCount;
    ++column;
    if (column >= kColumns) {
      column = 0;
      ++row;
    }
  }

  mSlotsContainer->setVisible(visibleSlotCount > 0);
  if (visibleSlotCount == 0) {
    mEmptyLabel->setText(tr("No quick slots available for the selected day."));
  }
  mEmptyLabel->setVisible(visibleSlotCount == 0);
}

bool QuickSlotsWidget::slotOverlapsAnyEvent(const QDateTime &slotStart,
                                            const QDateTime &slotEnd) const {
  const auto slotStartMs = slotStart.toMSecsSinceEpoch() -
                           pcm::app_settings::defaultBufferBeforeMinutes() * 60'000LL;
  const auto slotEndMs = slotEnd.toMSecsSinceEpoch() +
                         pcm::app_settings::defaultBufferAfterMinutes() * 60'000LL;

  for (const auto &[eventStart, eventEnd] : mBusyIntervals) {
    const auto eventStartMs = eventStart.toMSecsSinceEpoch();
    const auto eventEndMs = eventEnd.toMSecsSinceEpoch();
    if (eventStartMs < slotEndMs && eventEndMs > slotStartMs) {
      return true;
    }
  }

  return false;
}

void QuickSlotsWidget::clearSlots() {
  while (const auto item = mSlotsLayout->takeAt(0)) {
    delete item->widget();
    delete item;
  }
}
