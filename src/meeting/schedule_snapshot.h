#pragma once

#include "schedule/schedule.h"
#include "schema.hpp"

#include <QByteArray>

#include <optional>
#include <string>

namespace pcm::meeting {

// Local statuses that close joining: canceled (3), no-show (5), rescheduled (6).
[[nodiscard]] bool statusDisablesJoin(std::int64_t eventStatusId);

// Builds the full schedule snapshot from local state. Only schedule semantics
// leave the device: no database id, title, description, client, notes, cost or
// payment data. The whole-series join flag is ANDed into every override so a
// canceled series cannot be reopened through a moved occurrence.
// Returns nullopt when the explicit IANA timezone is unknown or the series has
// no usable start/end; the timezone is never guessed.
[[nodiscard]] std::optional<pcm::schedule::Snapshot>
buildScheduleSnapshot(const pcm::database::ScheduleSource &source);

// Queue form of buildScheduleSnapshot (revision and base_revision are 0; the
// sync service assigns them when the payload is frozen for sending).
[[nodiscard]] std::optional<std::string>
buildScheduleSnapshotPayload(const pcm::database::ScheduleSource &source);

// Wire format of PUT /v1/schedule-series/{uid} (schema_version 1).
[[nodiscard]] QByteArray serializeSnapshot(const pcm::schedule::Snapshot &snapshot);
// Strict inverse; every version 1 field is mandatory, including join_enabled.
[[nodiscard]] std::optional<pcm::schedule::Snapshot> parseSnapshot(const QByteArray &json);

// SHA-256 over the backend's canonical form (revision and base_revision are
// transport state and excluded; overrides/exceptions are sorted first).
[[nodiscard]] std::string scheduleContentHash(const pcm::schedule::Snapshot &snapshot);

} // namespace pcm::meeting
