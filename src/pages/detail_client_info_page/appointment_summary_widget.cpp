#include "appointment_summary_widget.h"

#include <QDateTime>
#include <QTimeZone>
#include <QVBoxLayout>

AppointmentSummaryWidget::AppointmentSummaryWidget(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 8);
  layout->setSpacing(2);

  mLastLabel = new QLabel(this);
  mLastLabel->setStyleSheet("color: rgba(255, 255, 255, 0.65);");
  mNextLabel = new QLabel(this);
  mNextLabel->setStyleSheet("color: rgba(255, 255, 255, 0.65);");

  layout->addWidget(mLastLabel);
  layout->addWidget(mNextLabel);

  clear();
}

namespace {
QString describe(const std::optional<DuckEvent> &event) {
  if (!event.has_value() || !event->start_date.has_value()) {
    return {};
  }
  return QDateTime::fromMSecsSinceEpoch(*event->start_date, QTimeZone(QTimeZone::LocalTime))
      .toString("dd.MM.yyyy HH:mm");
}
} // namespace

void AppointmentSummaryWidget::setAppointments(
    const pcm::recurrence::LastNextAppointment &summary) {
  const auto lastText = describe(summary.last);
  const auto nextText = describe(summary.next);

  mLastLabel->setText(lastText.isEmpty() ? tr("Last appointment: none")
                                         : tr("Last appointment: %1").arg(lastText));
  mNextLabel->setText(nextText.isEmpty() ? tr("Next appointment: none")
                                         : tr("Next appointment: %1").arg(nextText));
}

void AppointmentSummaryWidget::clear() {
  mLastLabel->setText(tr("Last appointment: none"));
  mNextLabel->setText(tr("Next appointment: none"));
}
