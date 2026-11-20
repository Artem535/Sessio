#pragma once
#include <QDate>
#include <QString>
#include <QTime>
#include <cstdint>
#include <optional>

// Values the new-event dialog opens with. Nothing is created until the user
// saves the dialog.
struct EventTemplate {
  QDate date;
  std::optional<QTime> time;
  std::optional<int> durationMinutes;
  QString title;
  QString notes; // the source text, e.g. the agreement (the dialog has no notes field)
  std::optional<int64_t> clientId;
};
