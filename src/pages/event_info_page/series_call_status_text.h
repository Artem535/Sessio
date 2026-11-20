#pragma once

#include "series_call_service.h"

#include <QString>
#include <QStringList>

namespace pcm::eventpage {

enum class StatusSeverity { Info, Warning, Error };

// What the event editor shows for a published recurring call series: plain
// language lines plus which manual actions make sense right now.
struct SeriesStatusView {
  QStringList lines;
  StatusSeverity severity = StatusSeverity::Info;
  bool canRetry = false;          // manual retry of sync / invitation / migration
  bool canReissue = false;        // the server holds a link this device lost
  bool canPublishThisDevice = false; // schedule conflict: explicit overwrite action
  bool linkReady = false;         // copy link / invite / passcode make sense
};

[[nodiscard]] SeriesStatusView describeSeriesCallStatus(const pcm::meeting::SeriesCallStatus &status);

} // namespace pcm::eventpage
