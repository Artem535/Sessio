#include "change_log_text.h"

#include "client_notes_page.h"

#include <QDateTime>
#include <QTimeZone>

namespace pcm::client_notes {

QString eventChangeStatusLabel(const int64_t eventStatusId) {
  switch (eventStatusId) {
  case 2:
    return ClientNotesPage::tr("Completed");
  case 3:
    return ClientNotesPage::tr("Canceled");
  case 4:
    return ClientNotesPage::tr("Confirmed");
  case 5:
    return ClientNotesPage::tr("No show");
  case 6:
    return ClientNotesPage::tr("Rescheduled");
  case 1:
  default:
    return ClientNotesPage::tr("Scheduled");
  }
}

QString eventChangePaymentLabel(const int64_t paymentStatusId) {
  switch (paymentStatusId) {
  case 2:
    return ClientNotesPage::tr("Paid");
  case 3:
    return ClientNotesPage::tr("Canceled");
  case 4:
    return ClientNotesPage::tr("Refunded");
  case 5:
    return ClientNotesPage::tr("Skipped");
  case 1:
  default:
    return ClientNotesPage::tr("Pending");
  }
}

QString changeLogLineText(const DuckEventChangeLog &entry) {
  switch (entry.change_kind) {
  case 1: {
    QString text = ClientNotesPage::tr("Status changed: %1 → %2")
                       .arg(eventChangeStatusLabel(entry.old_event_stat_id.value_or(1)),
                            eventChangeStatusLabel(entry.new_event_stat_id.value_or(1)));
    if ((entry.new_event_stat_id.value_or(0) == 3 || entry.new_event_stat_id.value_or(0) == 5) &&
        entry.cancellation_reason.has_value() && !entry.cancellation_reason->empty()) {
      text += QStringLiteral(" (%1)").arg(QString::fromStdString(*entry.cancellation_reason));
    }
    return text;
  }
  case 2:
    return ClientNotesPage::tr("Payment status changed: %1 → %2")
               .arg(eventChangePaymentLabel(entry.old_payment_stat_id.value_or(1)),
                    eventChangePaymentLabel(entry.new_payment_stat_id.value_or(1)));
  case 3: {
    const auto oldAt = entry.old_start_date.has_value()
                            ? QDateTime::fromMSecsSinceEpoch(*entry.old_start_date,
                                                             QTimeZone(QTimeZone::LocalTime))
                            : QDateTime{};
    const auto newAt = entry.new_start_date.has_value()
                            ? QDateTime::fromMSecsSinceEpoch(*entry.new_start_date,
                                                             QTimeZone(QTimeZone::LocalTime))
                            : QDateTime{};
    return ClientNotesPage::tr("Rescheduled from %1 to %2")
               .arg(oldAt.isValid() ? oldAt.toString("dd.MM.yyyy HH:mm")
                                    : ClientNotesPage::tr("Unknown time"),
                    newAt.isValid() ? newAt.toString("dd.MM.yyyy HH:mm")
                                    : ClientNotesPage::tr("Unknown time"));
  }
  default:
    return {};
  }
}

} // namespace pcm::client_notes
