// db_structs.hpp
#pragma once
#include "db_utils.hpp"
#include <cstdint>
#include <duckdb.hpp>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

using duckdb::DataChunk;
using duckdb::idx_t;
using duckdb::timestamp_t;
using namespace pcm::database;

template <typename T>
inline std::ostream &print_optional(std::ostream &os,
                                    const std::optional<T> &opt) {
  if (opt.has_value()) {
    os << *opt;
  } else {
    os << "nullopt";
  }
  return os;
}

struct DuckApplicationMetadata {
  std::int32_t schema_version = 1;
  std::int32_t backup_format_version = 1;
  std::string workspace_uuid;
  std::int64_t created_at = 0;
  std::int64_t last_migration_at = 0;

  DuckApplicationMetadata() = default;
  DuckApplicationMetadata(const duckdb::DataChunk &chunk, duckdb::idx_t index) {
    schema_version = db_utils::toInt32AsInt64(chunk.GetValue(0, index));
    backup_format_version = db_utils::toInt32AsInt64(chunk.GetValue(1, index));
    workspace_uuid = chunk.GetValue(2, index).ToString();
    created_at = db_utils::toOptionalTimestampMs(chunk.GetValue(3, index)).value_or(0);
    last_migration_at =
        db_utils::toOptionalTimestampMs(chunk.GetValue(4, index)).value_or(0);
  }
};

inline std::ostream &operator<<(std::ostream &os,
                                const DuckApplicationMetadata &metadata) {
  os << "DuckApplicationMetadata{schema_version=" << metadata.schema_version
     << ", backup_format_version=" << metadata.backup_format_version
     << ", workspace_uuid=" << metadata.workspace_uuid
     << ", created_at=" << metadata.created_at
     << ", last_migration_at=" << metadata.last_migration_at << "}";
  return os;
}

// --- DuckPaymentStatus ---
struct DuckPaymentStatus {
  std::int64_t id = -1;
  std::string name;
  DuckPaymentStatus() = default;
  DuckPaymentStatus(const duckdb::DataChunk &chunk, duckdb::idx_t index) {
    auto id_val = chunk.GetValue(0, index);
    auto name_val = chunk.GetValue(1, index);
    id = db_utils::toInt32AsInt64(id_val);
    name = name_val.ToString(); // NOT NULL in schema
  }
};
inline std::ostream &operator<<(std::ostream &os, const DuckPaymentStatus &s) {
  os << "DuckPaymentStatus{id=" << s.id << ", name=\"" << s.name << "\"}";
  return os;
}
// --- DuckEventStatus ---
struct DuckEventStatus {
  std::int64_t id = -1;
  std::string name;
  DuckEventStatus() = default;
  DuckEventStatus(const DataChunk &chunk, idx_t index) {
    id = db_utils::toInt32AsInt64(chunk.GetValue(0, index));
    name = chunk.GetValue(1, index).ToString();
  }
};
inline std::ostream &operator<<(std::ostream &os, const DuckEventStatus &s) {
  os << "DuckEventStatus{id=" << s.id << ", name=\"" << s.name << "\"}";
  return os;
}
// --- DuckClient ---
struct DuckClient {
  std::int64_t id = -1;
  std::optional<std::string> name = std::nullopt;
  std::optional<std::string> last_name = std::nullopt;
  std::optional<std::string> additional_info = std::nullopt;
  std::optional<std::string> diagnosis = std::nullopt;
  std::optional<std::int64_t> birthday_date = std::nullopt;
  std::optional<std::string> email = std::nullopt;
  std::optional<std::string> phone_number = std::nullopt;
  bool client_active = false;
  std::optional<std::string> country = std::nullopt;
  std::optional<std::string> city = std::nullopt;
  std::optional<std::string> time_zone = std::nullopt;
  DuckClient() = default;
  DuckClient(const duckdb::DataChunk &chunk, duckdb::idx_t index) {
    id = db_utils::toInt32AsInt64(chunk.GetValue(0, index));
    name = db_utils::toOptionalString(chunk.GetValue(1, index));
    last_name = db_utils::toOptionalString(chunk.GetValue(2, index));
    additional_info = db_utils::toOptionalString(chunk.GetValue(3, index));
    diagnosis = db_utils::toOptionalString(chunk.GetValue(4, index));
    birthday_date =
        db_utils::toOptionalTimestampMs(chunk.GetValue(5, index));
    email = db_utils::toOptionalString(chunk.GetValue(6, index));
    phone_number = db_utils::toOptionalString(chunk.GetValue(7, index));
    client_active = db_utils::toBool(chunk.GetValue(8, index));
    country = db_utils::toOptionalString(chunk.GetValue(9, index));
    city = db_utils::toOptionalString(chunk.GetValue(10, index));
    time_zone = db_utils::toOptionalString(chunk.GetValue(11, index));
  }
};
inline std::ostream &operator<<(std::ostream &os, const DuckClient &c) {
  os << "DuckClient{"
     << "id=" << c.id << ", "
     << "name=";
  print_optional(os, c.name) << ", "
                             << "last_name=";
  print_optional(os, c.last_name) << ", "
                                  << "email=";
  print_optional(os, c.email)
      << ", "
      << "client_active=" << (c.client_active ? "true" : "false") << ", "
      << "birthday_date=";
  print_optional(os, c.birthday_date) << ", "
                                      << "country=";
  print_optional(os, c.country) << ", "
                                << "city=";
  print_optional(os, c.city) << ", "
                             << "time_zone=";
  print_optional(os, c.time_zone) << "}";
  return os;
}

inline void readBufferMinutes(const duckdb::DataChunk &chunk,
                              const duckdb::idx_t index,
                              const duckdb::idx_t beforeColumn,
                              const duckdb::idx_t afterColumn,
                              std::int64_t &beforeMinutes,
                              std::int64_t &afterMinutes) {
  if (chunk.ColumnCount() > beforeColumn) {
    beforeMinutes = db_utils::toOptionalInt32AsInt64(
                          chunk.GetValue(beforeColumn, index))
                        .value_or(0);
  }
  if (chunk.ColumnCount() > afterColumn) {
    afterMinutes = db_utils::toOptionalInt32AsInt64(
                         chunk.GetValue(afterColumn, index))
                       .value_or(0);
  }
}

// --- DuckEvent ---
struct DuckEvent {
  std::int64_t id = -1;
  std::optional<std::string> name = std::nullopt;
  std::optional<std::string> description = std::nullopt;
  std::optional<std::string> client_name = std::nullopt;
  bool is_work_event = false;
  std::int64_t event_stat_id = -1;
  std::int64_t payment_stat_id = -1;
  std::optional<std::int64_t> start_date = std::nullopt;
  std::optional<std::int64_t> end_date = std::nullopt;
  std::optional<std::int64_t> duration = std::nullopt;
  std::optional<double> cost = std::nullopt;
  std::optional<std::int64_t> reminder_notified_at = std::nullopt;
  bool is_online = false;
  std::string meeting_url;
  std::optional<std::int64_t> series_id = std::nullopt;
  std::optional<std::int64_t> original_occurrence_start = std::nullopt;
  std::optional<std::string> cancellation_reason = std::nullopt;
  std::optional<std::string> canceled_by = std::nullopt;
  std::int64_t buffer_before_minutes = 0;
  std::int64_t buffer_after_minutes = 0;
  bool is_virtual_occurrence = false;
  std::optional<std::string> provider_kind = std::nullopt;
  std::optional<std::string> meeting_ref = std::nullopt;
  std::optional<std::string> invitation_state = std::nullopt;
  DuckEvent() = default;
  DuckEvent(const duckdb::DataChunk &chunk, duckdb::idx_t index) {
    id = db_utils::toInt32AsInt64(chunk.GetValue(0, index));
    name = db_utils::toOptionalString(chunk.GetValue(1, index));
    description = db_utils::toOptionalString(chunk.GetValue(2, index));
    is_work_event = db_utils::toBool(chunk.GetValue(3, index));
    event_stat_id = db_utils::toInt32AsInt64(chunk.GetValue(4, index));
    payment_stat_id = db_utils::toInt32AsInt64(chunk.GetValue(5, index));
    start_date = db_utils::toOptionalTimestampMs(chunk.GetValue(6, index));
    end_date = db_utils::toOptionalTimestampMs(chunk.GetValue(7, index));
    duration = db_utils::toOptionalInt32AsInt64(chunk.GetValue(8, index));
    cost = db_utils::toOptionalDouble(chunk.GetValue(9, index));
    if (chunk.ColumnCount() > 10) {
      reminder_notified_at =
          db_utils::toOptionalTimestampMs(chunk.GetValue(10, index));
    }
    if (chunk.ColumnCount() > 11) {
      const auto onlineValue = chunk.GetValue(11, index);
      is_online = !onlineValue.IsNull() && db_utils::toBool(onlineValue);
    }
    if (chunk.ColumnCount() > 12) {
      meeting_url = db_utils::toOptionalString(chunk.GetValue(12, index)).value_or("");
    }
    if (chunk.ColumnCount() > 13) {
      series_id = db_utils::toOptionalInt32AsInt64(chunk.GetValue(13, index));
    }
    if (chunk.ColumnCount() > 14) {
      original_occurrence_start =
          db_utils::toOptionalTimestampMs(chunk.GetValue(14, index));
    }
    if (chunk.ColumnCount() > 15) {
      cancellation_reason =
          db_utils::toOptionalString(chunk.GetValue(15, index));
    }
    if (chunk.ColumnCount() > 16) {
      canceled_by = db_utils::toOptionalString(chunk.GetValue(16, index));
    }
    readBufferMinutes(chunk, index, 17, 18, buffer_before_minutes,
                      buffer_after_minutes);
    if (chunk.ColumnCount() > 19) {
      provider_kind = db_utils::toOptionalString(chunk.GetValue(19, index));
    }
    if (chunk.ColumnCount() > 20) {
      meeting_ref = db_utils::toOptionalString(chunk.GetValue(20, index));
    }
    if (chunk.ColumnCount() > 21) {
      invitation_state = db_utils::toOptionalString(chunk.GetValue(21, index));
    }
  }
};
inline std::ostream &operator<<(std::ostream &os, const DuckEvent &e) {
  os << "DuckEvent{"
     << "id=" << e.id << ", "
     << "name=";
  print_optional(os, e.name)
      << ", "
      << "is_work_event=" << (e.is_work_event ? "true" : "false") << ", "
      << "event_stat_id=" << e.event_stat_id << ", "
      << "payment_stat_id=" << e.payment_stat_id << ", "
      << "start_date=";
  print_optional(os, e.start_date) << ", "
                                   << "end_date=";
  print_optional(os, e.end_date) << ", "
                                 << "duration=";
  print_optional(os, e.duration) << ", "
                                 << "cost=";
  print_optional(os, e.cost) << ", "
                             << "reminder_notified_at=";
  print_optional(os, e.reminder_notified_at)
      << ", "
      << "is_online=" << (e.is_online ? "true" : "false") << ", "
      << "meeting_url=\"" << e.meeting_url << "\", "
      << "series_id=";
  print_optional(os, e.series_id) << ", "
                                  << "original_occurrence_start=";
  print_optional(os, e.original_occurrence_start)
      << ", "
      << "is_virtual_occurrence=" << (e.is_virtual_occurrence ? "true" : "false")
      << "}";
  return os;
}

// --- DuckEventSeries ---
struct DuckEventSeries {
  std::int64_t id = -1;
  std::optional<std::string> name = std::nullopt;
  std::optional<std::string> description = std::nullopt;
  std::optional<std::string> client_name = std::nullopt;
  std::optional<std::int64_t> client_id = std::nullopt;
  bool is_work_event = false;
  std::int64_t event_stat_id = -1;
  std::int64_t payment_stat_id = -1;
  std::optional<std::int64_t> start_date = std::nullopt;
  std::optional<std::int64_t> end_date = std::nullopt;
  std::optional<std::int64_t> duration = std::nullopt;
  std::optional<double> cost = std::nullopt;
  bool is_online = false;
  std::string meeting_url;
  std::string recurrence_rule;
  std::optional<std::int64_t> recurrence_until = std::nullopt;
  bool active = true;
  std::optional<std::string> cancellation_reason = std::nullopt;
  std::optional<std::string> canceled_by = std::nullopt;
  std::int64_t buffer_before_minutes = 0;
  std::int64_t buffer_after_minutes = 0;
  std::optional<std::string> provider_kind = std::nullopt;
  std::optional<std::string> meeting_ref = std::nullopt;
  std::optional<std::string> invitation_state = std::nullopt;

  DuckEventSeries() = default;
  DuckEventSeries(const duckdb::DataChunk &chunk, duckdb::idx_t index) {
    id = db_utils::toInt32AsInt64(chunk.GetValue(0, index));
    name = db_utils::toOptionalString(chunk.GetValue(1, index));
    description = db_utils::toOptionalString(chunk.GetValue(2, index));
    client_id = db_utils::toOptionalInt32AsInt64(chunk.GetValue(3, index));
    is_work_event = db_utils::toBool(chunk.GetValue(4, index));
    event_stat_id = db_utils::toInt32AsInt64(chunk.GetValue(5, index));
    payment_stat_id = db_utils::toInt32AsInt64(chunk.GetValue(6, index));
    start_date = db_utils::toOptionalTimestampMs(chunk.GetValue(7, index));
    end_date = db_utils::toOptionalTimestampMs(chunk.GetValue(8, index));
    duration = db_utils::toOptionalInt32AsInt64(chunk.GetValue(9, index));
    cost = db_utils::toOptionalDouble(chunk.GetValue(10, index));
    is_online = db_utils::toBool(chunk.GetValue(11, index));
    meeting_url = db_utils::toOptionalString(chunk.GetValue(12, index)).value_or("");
    recurrence_rule = chunk.GetValue(13, index).ToString();
    recurrence_until = db_utils::toOptionalTimestampMs(chunk.GetValue(14, index));
    active = db_utils::toBool(chunk.GetValue(15, index));
    if (chunk.ColumnCount() > 16) {
      cancellation_reason =
          db_utils::toOptionalString(chunk.GetValue(16, index));
    }
    if (chunk.ColumnCount() > 17) {
      canceled_by = db_utils::toOptionalString(chunk.GetValue(17, index));
    }
    readBufferMinutes(chunk, index, 20, 21, buffer_before_minutes,
                      buffer_after_minutes);
    if (chunk.ColumnCount() > 22) {
      provider_kind = db_utils::toOptionalString(chunk.GetValue(22, index));
    }
    if (chunk.ColumnCount() > 23) {
      meeting_ref = db_utils::toOptionalString(chunk.GetValue(23, index));
    }
    if (chunk.ColumnCount() > 24) {
      invitation_state = db_utils::toOptionalString(chunk.GetValue(24, index));
    }
  }
};
// --- DuckEventClient ---
struct DuckEventClient {
  std::int64_t id = -1;
  std::int64_t client_id = -1;
  std::int64_t event_id = -1;
  DuckEventClient() = default;
  DuckEventClient(const duckdb::DataChunk &chunk, duckdb::idx_t index) {
    id = db_utils::toInt32AsInt64(chunk.GetValue(0, index));
    client_id = db_utils::toInt32AsInt64(chunk.GetValue(1, index));
    event_id = db_utils::toInt32AsInt64(chunk.GetValue(2, index));
  }
};
inline std::ostream &operator<<(std::ostream &os, const DuckEventClient &ec) {
  os << "DuckEventClient{id=" << ec.id << ", client_id=" << ec.client_id
     << ", event_id=" << ec.event_id << "}";
  return os;
}

// --- DuckEventChangeLog ---
struct DuckEventChangeLog {
  std::int64_t id = -1;
  std::int64_t event_id = -1;
  std::int64_t change_kind = 0; // 1=status, 2=payment, 3=reschedule
  std::optional<std::int64_t> old_event_stat_id = std::nullopt;
  std::optional<std::int64_t> new_event_stat_id = std::nullopt;
  std::optional<std::int64_t> old_payment_stat_id = std::nullopt;
  std::optional<std::int64_t> new_payment_stat_id = std::nullopt;
  std::optional<std::int64_t> old_start_date = std::nullopt;
  std::optional<std::int64_t> new_start_date = std::nullopt;
  std::optional<std::string> cancellation_reason = std::nullopt;
  std::int64_t occurred_at = 0;
  std::optional<std::int64_t> event_current_start_date = std::nullopt;

  DuckEventChangeLog() = default;
  DuckEventChangeLog(const duckdb::DataChunk &chunk, duckdb::idx_t index) {
    id = db_utils::toInt32AsInt64(chunk.GetValue(0, index));
    event_id = db_utils::toInt32AsInt64(chunk.GetValue(1, index));
    change_kind = db_utils::toInt32AsInt64(chunk.GetValue(2, index));
    old_event_stat_id = db_utils::toOptionalInt32AsInt64(chunk.GetValue(3, index));
    new_event_stat_id = db_utils::toOptionalInt32AsInt64(chunk.GetValue(4, index));
    old_payment_stat_id = db_utils::toOptionalInt32AsInt64(chunk.GetValue(5, index));
    new_payment_stat_id = db_utils::toOptionalInt32AsInt64(chunk.GetValue(6, index));
    old_start_date = db_utils::toOptionalTimestampMs(chunk.GetValue(7, index));
    new_start_date = db_utils::toOptionalTimestampMs(chunk.GetValue(8, index));
    cancellation_reason = db_utils::toOptionalString(chunk.GetValue(9, index));
    occurred_at = db_utils::toOptionalTimestampMs(chunk.GetValue(10, index)).value_or(0);
    if (chunk.ColumnCount() > 11) {
      event_current_start_date =
          db_utils::toOptionalTimestampMs(chunk.GetValue(11, index));
    }
  }
};
inline std::ostream &operator<<(std::ostream &os, const DuckEventChangeLog &entry) {
  os << "DuckEventChangeLog{id=" << entry.id << ", event_id=" << entry.event_id
     << ", change_kind=" << entry.change_kind << ", occurred_at=" << entry.occurred_at
     << "}";
  return os;
}

// --- DuckClientNote ---
struct DuckClientNote {
  std::int64_t id = -1;
  std::int64_t client_id = -1;
  std::optional<std::string> body_markdown = std::nullopt;
  std::optional<std::int64_t> created_at = std::nullopt;
  std::optional<std::int64_t> updated_at = std::nullopt;
  std::optional<std::int64_t> linked_event_id = std::nullopt;
  std::optional<std::int64_t> linked_series_id = std::nullopt;
  std::optional<std::int64_t> linked_occurrence_start_ms = std::nullopt;

  DuckClientNote() = default;
  DuckClientNote(const duckdb::DataChunk &chunk, duckdb::idx_t index) {
    id = db_utils::toInt32AsInt64(chunk.GetValue(0, index));
    client_id = db_utils::toInt32AsInt64(chunk.GetValue(1, index));
    body_markdown = db_utils::toOptionalString(chunk.GetValue(2, index));
    created_at = db_utils::toOptionalTimestampMs(chunk.GetValue(3, index));
    updated_at = db_utils::toOptionalTimestampMs(chunk.GetValue(4, index));
    if (chunk.ColumnCount() > 5) {
      linked_event_id = db_utils::toOptionalInt32AsInt64(chunk.GetValue(5, index));
    }
    if (chunk.ColumnCount() > 6) {
      linked_series_id = db_utils::toOptionalInt32AsInt64(chunk.GetValue(6, index));
    }
    if (chunk.ColumnCount() > 7) {
      linked_occurrence_start_ms =
          db_utils::toOptionalTimestampMs(chunk.GetValue(7, index));
    }
  }
};
inline std::ostream &operator<<(std::ostream &os, const DuckClientNote &note) {
  os << "DuckClientNote{id=" << note.id << ", client_id=" << note.client_id
     << ", body_markdown=";
  print_optional(os, note.body_markdown) << ", created_at=";
  print_optional(os, note.created_at) << ", updated_at=";
  print_optional(os, note.updated_at) << ", linked_event_id=";
  print_optional(os, note.linked_event_id) << ", linked_series_id=";
  print_optional(os, note.linked_series_id) << ", linked_occurrence_start_ms=";
  print_optional(os, note.linked_occurrence_start_ms) << "}";
  return os;
}

// --- DuckTranscript ---
// status: "recording" | "draft" | "reviewed". Times are epoch milliseconds.
struct DuckTranscript {
  std::int64_t id = -1;
  std::int64_t event_id = -1;
  std::string status;
  std::string consent_scope;
  std::int64_t consent_given_at = 0;
  std::optional<std::int64_t> consent_revoked_at = std::nullopt;
  std::optional<std::string> model_id = std::nullopt;
  std::int64_t created_at = 0;
  std::int64_t updated_at = 0;

  DuckTranscript() = default;
  DuckTranscript(const duckdb::DataChunk &chunk, duckdb::idx_t index) {
    id = db_utils::toInt32AsInt64(chunk.GetValue(0, index));
    event_id = db_utils::toInt32AsInt64(chunk.GetValue(1, index));
    status = chunk.GetValue(2, index).ToString();
    consent_scope = chunk.GetValue(3, index).ToString();
    consent_given_at = db_utils::toOptionalTimestampMs(chunk.GetValue(4, index)).value_or(0);
    consent_revoked_at = db_utils::toOptionalTimestampMs(chunk.GetValue(5, index));
    model_id = db_utils::toOptionalString(chunk.GetValue(6, index));
    created_at = db_utils::toOptionalTimestampMs(chunk.GetValue(7, index)).value_or(0);
    updated_at = db_utils::toOptionalTimestampMs(chunk.GetValue(8, index)).value_or(0);
  }
};

// --- DuckTranscriptPhrase ---
// start_ms / end_ms are milliseconds from the start of the call.
struct DuckTranscriptPhrase {
  std::int64_t id = -1;
  std::int64_t transcript_id = -1;
  std::string track_role;  // "practitioner" | "participant"
  std::optional<std::string> speaker_name = std::nullopt;
  std::int64_t start_ms = 0;
  std::int64_t end_ms = 0;
  std::string text;
  bool edited = false;

  DuckTranscriptPhrase() = default;
  DuckTranscriptPhrase(const duckdb::DataChunk &chunk, duckdb::idx_t index) {
    id = db_utils::toInt32AsInt64(chunk.GetValue(0, index));
    transcript_id = db_utils::toInt32AsInt64(chunk.GetValue(1, index));
    track_role = chunk.GetValue(2, index).ToString();
    speaker_name = db_utils::toOptionalString(chunk.GetValue(3, index));
    start_ms = chunk.GetValue(4, index).GetValue<int64_t>();
    end_ms = chunk.GetValue(5, index).GetValue<int64_t>();
    text = chunk.GetValue(6, index).ToString();
    const auto edited_value = chunk.GetValue(7, index);
    edited = !edited_value.IsNull() && db_utils::toBool(edited_value);
  }
};

// --- DuckClientNoteAttachment ---
struct DuckClientNoteAttachment {
  std::int64_t id = -1;
  std::int64_t note_id = -1;
  std::optional<std::string> file_name = std::nullopt;
  std::optional<std::string> relative_path = std::nullopt;
  std::optional<std::string> mime_type = std::nullopt;
  std::optional<std::int64_t> size_bytes = std::nullopt;
  std::optional<std::int64_t> created_at = std::nullopt;

  DuckClientNoteAttachment() = default;
  DuckClientNoteAttachment(const duckdb::DataChunk &chunk, duckdb::idx_t index) {
    id = db_utils::toInt32AsInt64(chunk.GetValue(0, index));
    note_id = db_utils::toInt32AsInt64(chunk.GetValue(1, index));
    file_name = db_utils::toOptionalString(chunk.GetValue(2, index));
    relative_path = db_utils::toOptionalString(chunk.GetValue(3, index));
    mime_type = db_utils::toOptionalString(chunk.GetValue(4, index));
    size_bytes = db_utils::toOptionalInt32AsInt64(chunk.GetValue(5, index));
    created_at = db_utils::toOptionalTimestampMs(chunk.GetValue(6, index));
  }
};
inline std::ostream &operator<<(std::ostream &os,
                                const DuckClientNoteAttachment &attachment) {
  os << "DuckClientNoteAttachment{id=" << attachment.id
     << ", note_id=" << attachment.note_id << ", file_name=";
  print_optional(os, attachment.file_name) << ", relative_path=";
  print_optional(os, attachment.relative_path) << ", mime_type=";
  print_optional(os, attachment.mime_type) << ", size_bytes=";
  print_optional(os, attachment.size_bytes) << ", created_at=";
  print_optional(os, attachment.created_at) << "}";
  return os;
}

namespace pcm::database {

namespace schedule_sync_state {
// Persisted per-series delivery state. "Sending" and "waiting for network" are
// transient and live only in the sync service.
inline constexpr char kPending[] = "pending";
inline constexpr char kSynced[] = "synced";
inline constexpr char kConflict[] = "conflict";
inline constexpr char kRejected[] = "rejected";
} // namespace schedule_sync_state

// Local schedule identity of a recurring series that is published to the
// token backend. series_uid never changes after creation.
struct ScheduleIdentity {
  std::int64_t series_id = -1;
  std::string series_uid;
  std::string timezone;
  std::int64_t invitation_generation = 0;
  std::optional<std::string> invitation_key = std::nullopt;
  std::int64_t desired_revision = 0;
  std::int64_t acked_revision = 0;
  std::string acked_content_hash;
  std::string sync_state = schedule_sync_state::kPending;
  std::string last_error;
};

struct ScheduleOutbox {
  std::string series_uid;
  std::optional<std::string> pending_payload = std::nullopt;
  std::optional<std::int64_t> pending_desired_revision = std::nullopt;
  std::optional<std::int64_t> inflight_revision = std::nullopt;
  std::optional<std::string> inflight_payload = std::nullopt;
  std::optional<std::string> inflight_hash = std::nullopt;
  std::optional<std::int64_t> inflight_desired_revision = std::nullopt;
};

// Materialized per-occurrence Event row of a series (moved or status-changed).
struct ScheduleOverrideSource {
  std::int64_t original_start_ms = 0;
  std::int64_t start_ms = 0;
  std::int64_t end_ms = 0;
  std::int64_t event_stat_id = 1;
};

// Everything the schedule snapshot builder may read. It carries no client
// reference, notes or payment data beyond what DuckEventSeries already holds;
// the builder decides what leaves the device.
struct ScheduleSource {
  DuckEventSeries series;
  ScheduleIdentity identity;
  std::vector<ScheduleOverrideSource> overrides;
  std::vector<std::int64_t> exceptions;
};

struct ScheduleCommit {
  std::int64_t series_id = -1;
  std::string series_uid; // empty for legacy series without schedule identity
  std::int64_t desired_revision = 0;
};

struct ScheduleAck {
  bool has_newer_pending = false;
};

} // namespace pcm::database
