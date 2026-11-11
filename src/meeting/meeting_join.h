#pragma once

#include "schema.hpp"

#include <QString>
#include <optional>

namespace pcm::meeting {

/// What "join this meeting" should do for an event.
struct JoinAction {
  enum class Kind { None, LiveKit, Url, JitsiUrl } kind = Kind::None;
  QString target;
};

/// Shared by the Calendar inspector and «Мой день».
/// @param seriesJoinTarget SeriesCallService::joinTargetFor(event) result
///        (nullopt if none / no service).
/// Order: engaged series target -> LiveKit(target), or None if it is empty; provider LiveKit with
/// meeting_ref -> LiveKit; non-empty meeting_url -> Url; else None.
[[nodiscard]] JoinAction resolveJoinAction(const DuckEvent &event,
                                           const std::optional<QString> &seriesJoinTarget);

} // namespace pcm::meeting
