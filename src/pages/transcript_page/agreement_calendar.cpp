#include "agreement_calendar.h"
#include <QDateTime>

EventTemplate agreementEventTemplate(const QJsonObject &record, const std::optional<DuckEvent> &callEvent,
                                     std::optional<int64_t> clientId, const QString &clientName,
                                     const QDate &callDate) {
  EventTemplate t;
  const auto text = record["text"].toString().trimmed();
  t.date = QDate::fromString(record["date"].toString(), Qt::ISODate);
  if (!t.date.isValid()) t.date = callDate.addDays(7);
  if (const auto time = QTime::fromString(record["time"].toString(), QStringLiteral("HH:mm")); time.isValid())
    t.time = time;
  const auto start = callEvent && callEvent->start_date
      ? QDateTime::fromMSecsSinceEpoch(*callEvent->start_date).toLocalTime() : QDateTime();
  if (!t.time && start.isValid()) t.time = start.time();
  if (start.isValid() && callEvent->end_date && *callEvent->end_date > *callEvent->start_date)
    t.durationMinutes = static_cast<int>((*callEvent->end_date - *callEvent->start_date) / 60000);
  const auto eventName = callEvent ? QString::fromStdString(callEvent->name.value_or("")).trimmed() : QString();
  t.title = !eventName.isEmpty() ? eventName : !clientName.trimmed().isEmpty() ? clientName.trimmed() : text;
  t.notes = text;
  t.clientId = clientId;
  return t;
}
