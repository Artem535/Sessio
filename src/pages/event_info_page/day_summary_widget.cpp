#include "day_summary_widget.h"

#include <QDateTime>
#include <QFont>
#include <QLocale>
#include <QPushButton>
#include <QTimeZone>

DaySummaryWidget::DaySummaryWidget(QWidget *parent) : QWidget(parent) {
  setObjectName("daySummaryCard");
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);

  mDateLabel = new QLabel(this);
  mDateLabel->setObjectName("daySummaryDate");
  mDateLabel->setWordWrap(true);
  QFont dateFont = mDateLabel->font();
  dateFont.setBold(true);
  dateFont.setPointSize(18);
  mDateLabel->setFont(dateFont);

  mCountsLabel = new QLabel(this);
  mCountsLabel->setStyleSheet("color: rgba(255, 255, 255, 0.75);");
  mBusyLabel = new QLabel(this);
  mBusyLabel->setStyleSheet("color: rgba(255, 255, 255, 0.65);");
  mNextSessionLabel = new QLabel(this);
  mNextSessionLabel->setStyleSheet("color: rgba(255, 255, 255, 0.65);");
  mFreeWindowLabel = new QLabel(this);
  mFreeWindowLabel->setStyleSheet("color: rgba(255, 255, 255, 0.65);");

  layout->addWidget(mDateLabel);
  layout->addSpacing(6);
  layout->addWidget(mCountsLabel);
  layout->addWidget(mBusyLabel);
  layout->addWidget(mNextSessionLabel);
  layout->addWidget(mFreeWindowLabel);

  mMiniListLayout = new QVBoxLayout();
  mMiniListLayout->setContentsMargins(0, 6, 0, 0);
  mMiniListLayout->setSpacing(4);
  layout->addLayout(mMiniListLayout);
  layout->addStretch(1);
}

void DaySummaryWidget::clearMiniList() {
  while (auto *item = mMiniListLayout->takeAt(0)) {
    delete item->widget();
    delete item;
  }
}

void DaySummaryWidget::setSummary(const pcm::recurrence::DaySummary &summary) {
  clearMiniList();

  auto heading = QLocale().toString(summary.date, QLocale::LongFormat);
  if (!heading.isEmpty()) {
    heading[0] = heading.at(0).toUpper(); // "пятница" -> "Пятница"
  }
  mDateLabel->setText(heading);

  if (!summary.hasSessions) {
    mCountsLabel->setText(tr("Free all day"));
    mBusyLabel->clear();
    mBusyLabel->setVisible(false);
    mNextSessionLabel->clear();
    mNextSessionLabel->setVisible(false);
    mFreeWindowLabel->clear();
    mFreeWindowLabel->setVisible(false);
    return;
  }

  mCountsLabel->setText(tr("%n session(s) · %1 client(s)", "", summary.sessionCount)
                            .arg(summary.clientCount));

  const auto busyHours = summary.busyMinutes / 60;
  const auto busyMinutesRemainder = summary.busyMinutes % 60;
  mBusyLabel->setText(tr("Busy: %1h %2m").arg(busyHours).arg(busyMinutesRemainder));
  mBusyLabel->setVisible(true);

  if (summary.nextSession.has_value() && summary.nextSession->start_date.has_value()) {
    const auto nextAt = QDateTime::fromMSecsSinceEpoch(*summary.nextSession->start_date,
                                                        QTimeZone::systemTimeZone());
    mNextSessionLabel->setText(tr("Next session: %1").arg(nextAt.toString("HH:mm")));
    mNextSessionLabel->setVisible(true);
  } else {
    mNextSessionLabel->clear();
    mNextSessionLabel->setVisible(false);
  }

  if (summary.freeWindowStart.has_value() && summary.freeWindowEnd.has_value()) {
    mFreeWindowLabel->setText(tr("Nearest free window: %1-%2")
                                  .arg(summary.freeWindowStart->toString("HH:mm"),
                                       summary.freeWindowEnd->toString("HH:mm")));
    mFreeWindowLabel->setVisible(true);
  } else {
    mFreeWindowLabel->clear();
    mFreeWindowLabel->setVisible(false);
  }

  for (const auto &event : summary.upcoming) {
    if (!event.start_date.has_value()) {
      continue;
    }
    const auto startAt =
        QDateTime::fromMSecsSinceEpoch(*event.start_date, QTimeZone::systemTimeZone());
    const auto clientName =
        QString::fromStdString(event.client_name.value_or(std::string{}));
    auto *row = new QPushButton(this);
    row->setFlat(true);
    row->setCursor(Qt::PointingHandCursor);
    row->setText(clientName.isEmpty()
                     ? startAt.toString("HH:mm")
                     : QString("%1  %2").arg(startAt.toString("HH:mm"), clientName));
    row->setStyleSheet(
        "QPushButton { text-align: left; color: rgba(255, 255, 255, 0.90); "
        "background: transparent; border: none; padding: 2px 0px; }");

    const auto eventId = event.id;
    connect(row, &QPushButton::clicked, this,
            [this, eventId]() { emit eventHighlightRequested(eventId); });

    mMiniListLayout->addWidget(row);
  }
}
