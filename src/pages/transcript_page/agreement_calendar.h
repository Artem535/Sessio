#pragma once
#include "../event_info_page/event_template.h"
#include "schema.hpp"
#include <QJsonObject>

// The new-event template for an accepted meeting agreement:
// - date: the agreement's date, else a week after the call;
// - time: the agreement's time, else the call event's start time;
// - duration: the call event's duration, if known;
// - title: the call event's title, else the client's name, else the agreement text;
// - notes: the agreement text; client: the call's client.
EventTemplate agreementEventTemplate(const QJsonObject &record, const std::optional<DuckEvent> &callEvent,
                                     std::optional<int64_t> clientId, const QString &clientName,
                                     const QDate &callDate);
