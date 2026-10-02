// constance.hpp

#pragma once

namespace pcm::database::constance {

constexpr auto kCreateTables = R"duckdb(
CREATE TABLE IF NOT EXISTS ApplicationMetadata (
    id INTEGER PRIMARY KEY,
    schema_version INTEGER NOT NULL,
    backup_format_version INTEGER NOT NULL,
    workspace_uuid TEXT NOT NULL,
    created_at TIMESTAMP NOT NULL,
    last_migration_at TIMESTAMP NOT NULL
);

-- Payment statuses
CREATE TABLE IF NOT EXISTS PaymentStatus (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL
);

-- Event statuses
CREATE TABLE IF NOT EXISTS EventStatus (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL
);

-- Clients
CREATE TABLE IF NOT EXISTS Client (
    id INTEGER PRIMARY KEY,
    name TEXT,
    last_name TEXT,
    additional_info TEXT,
    diagnosis TEXT,
    birthday_date TIMESTAMP,
    email TEXT,
    phone_number TEXT,
    client_active BOOLEAN,
    country TEXT,
    city TEXT,
    time_zone TEXT
);

-- Events
CREATE TABLE IF NOT EXISTS Event (
    id INTEGER PRIMARY KEY,
    name TEXT,
    description TEXT,
    is_work_event BOOLEAN,
    event_stat_id INTEGER REFERENCES EventStatus(id),
    payment_stat_id INTEGER REFERENCES PaymentStatus(id),
    start_date TIMESTAMP,
    end_date TIMESTAMP,
    duration INTEGER,
    cost DOUBLE,
    reminder_notified_at TIMESTAMP,
    is_online BOOLEAN DEFAULT FALSE,
    meeting_url TEXT,
    series_id INTEGER,
    original_occurrence_start TIMESTAMP,
    cancellation_reason TEXT,
    canceled_by TEXT,
    buffer_before_minutes INTEGER DEFAULT 0,
    buffer_after_minutes INTEGER DEFAULT 0,
    provider_kind TEXT,
    meeting_ref TEXT,
    invitation_state TEXT
);

CREATE TABLE IF NOT EXISTS EventSeries (
    id INTEGER PRIMARY KEY,
    name TEXT,
    description TEXT,
    client_id INTEGER REFERENCES Client(id),
    is_work_event BOOLEAN,
    event_stat_id INTEGER REFERENCES EventStatus(id),
    payment_stat_id INTEGER REFERENCES PaymentStatus(id),
    start_date TIMESTAMP,
    end_date TIMESTAMP,
    duration INTEGER,
    cost DOUBLE,
    is_online BOOLEAN DEFAULT FALSE,
    meeting_url TEXT,
    recurrence_rule TEXT NOT NULL,
    recurrence_until TIMESTAMP,
    active BOOLEAN DEFAULT TRUE,
    created_at TIMESTAMP,
    updated_at TIMESTAMP,
    cancellation_reason TEXT,
    canceled_by TEXT,
    buffer_before_minutes INTEGER DEFAULT 0,
    buffer_after_minutes INTEGER DEFAULT 0,
    provider_kind TEXT,
    meeting_ref TEXT,
    invitation_state TEXT
);

CREATE TABLE IF NOT EXISTS EventSeriesException (
    id INTEGER PRIMARY KEY,
    series_id INTEGER NOT NULL REFERENCES EventSeries(id),
    occurrence_start TIMESTAMP NOT NULL,
    reason TEXT,
    UNIQUE (series_id, occurrence_start)
);

CREATE TABLE IF NOT EXISTS EventSeriesOccurrenceReminder (
    series_id INTEGER NOT NULL REFERENCES EventSeries(id),
    occurrence_start TIMESTAMP NOT NULL,
    notified_at TIMESTAMP NOT NULL,
    PRIMARY KEY (series_id, occurrence_start)
);

-- Many-to-many relationship between clients and events
CREATE TABLE IF NOT EXISTS EventClient (
    id INTEGER PRIMARY KEY,
    client_id INTEGER NOT NULL REFERENCES Client(id),
    event_id INTEGER NOT NULL REFERENCES Event(id),
    UNIQUE (client_id, event_id)
);

CREATE TABLE IF NOT EXISTS ClientNote (
    id INTEGER PRIMARY KEY,
    client_id INTEGER NOT NULL REFERENCES Client(id),
    body_markdown TEXT,
    created_at TIMESTAMP NOT NULL,
    updated_at TIMESTAMP NOT NULL
);

CREATE TABLE IF NOT EXISTS ClientNoteAttachment (
    id INTEGER PRIMARY KEY,
    note_id INTEGER NOT NULL REFERENCES ClientNote(id),
    file_name TEXT,
    relative_path TEXT,
    mime_type TEXT,
    size_bytes BIGINT,
    created_at TIMESTAMP NOT NULL
);

CREATE TABLE IF NOT EXISTS EventChangeLog (
    id INTEGER PRIMARY KEY,
    event_id INTEGER NOT NULL REFERENCES Event(id),
    change_kind INTEGER NOT NULL, -- 1=status, 2=payment, 3=reschedule
    old_event_stat_id INTEGER,
    new_event_stat_id INTEGER,
    old_payment_stat_id INTEGER,
    new_payment_stat_id INTEGER,
    old_start_date TIMESTAMP,
    new_start_date TIMESTAMP,
    cancellation_reason TEXT,
    occurred_at TIMESTAMP NOT NULL
);

-- Local identity and delivery state of a recurring series published to the
-- token backend. No foreign key: the identity must survive series edits that
-- DuckDB would otherwise treat as key updates.
CREATE TABLE IF NOT EXISTS ScheduleSeries (
    series_id INTEGER PRIMARY KEY,
    series_uid TEXT NOT NULL,
    timezone TEXT NOT NULL,
    invitation_generation BIGINT NOT NULL,
    invitation_key TEXT,
    desired_revision BIGINT NOT NULL,
    acked_revision BIGINT NOT NULL,
    acked_content_hash TEXT,
    sync_state TEXT NOT NULL,
    last_error TEXT,
    created_at TIMESTAMP NOT NULL,
    updated_at TIMESTAMP NOT NULL
);

-- One row per series: the latest queued snapshot and the immutable payload
-- currently owned by a network request.
CREATE TABLE IF NOT EXISTS ScheduleOutbox (
    series_uid TEXT PRIMARY KEY,
    pending_payload TEXT,
    pending_desired_revision BIGINT,
    inflight_revision BIGINT,
    inflight_payload TEXT,
    inflight_hash TEXT,
    inflight_desired_revision BIGINT,
    updated_at TIMESTAMP NOT NULL
);
)duckdb";

constexpr auto kSchemaMigrations = R"duckdb(
ALTER TABLE ApplicationMetadata ADD COLUMN IF NOT EXISTS schema_version INTEGER;
ALTER TABLE ApplicationMetadata ADD COLUMN IF NOT EXISTS backup_format_version INTEGER;
ALTER TABLE ApplicationMetadata ADD COLUMN IF NOT EXISTS workspace_uuid TEXT;
ALTER TABLE ApplicationMetadata ADD COLUMN IF NOT EXISTS created_at TIMESTAMP;
ALTER TABLE ApplicationMetadata ADD COLUMN IF NOT EXISTS last_migration_at TIMESTAMP;

ALTER TABLE Event ADD COLUMN IF NOT EXISTS cost DOUBLE;
ALTER TABLE Event ADD COLUMN IF NOT EXISTS reminder_notified_at TIMESTAMP;
ALTER TABLE Event ADD COLUMN IF NOT EXISTS is_online BOOLEAN DEFAULT FALSE;
ALTER TABLE Event ADD COLUMN IF NOT EXISTS meeting_url TEXT;
ALTER TABLE Event ADD COLUMN IF NOT EXISTS series_id INTEGER;
ALTER TABLE Event ADD COLUMN IF NOT EXISTS original_occurrence_start TIMESTAMP;
ALTER TABLE Event ADD COLUMN IF NOT EXISTS cancellation_reason TEXT;
ALTER TABLE Event ADD COLUMN IF NOT EXISTS canceled_by TEXT;
ALTER TABLE Event ADD COLUMN IF NOT EXISTS buffer_before_minutes INTEGER DEFAULT 0;
ALTER TABLE Event ADD COLUMN IF NOT EXISTS buffer_after_minutes INTEGER DEFAULT 0;
UPDATE Event SET buffer_before_minutes = 0 WHERE buffer_before_minutes IS NULL;
UPDATE Event SET buffer_after_minutes = 0 WHERE buffer_after_minutes IS NULL;

ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS name TEXT;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS description TEXT;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS client_id INTEGER;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS is_work_event BOOLEAN;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS event_stat_id INTEGER;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS payment_stat_id INTEGER;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS start_date TIMESTAMP;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS end_date TIMESTAMP;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS duration INTEGER;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS cost DOUBLE;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS is_online BOOLEAN DEFAULT FALSE;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS meeting_url TEXT;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS recurrence_rule TEXT;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS recurrence_until TIMESTAMP;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS active BOOLEAN DEFAULT TRUE;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS created_at TIMESTAMP;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS updated_at TIMESTAMP;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS cancellation_reason TEXT;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS canceled_by TEXT;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS buffer_before_minutes INTEGER DEFAULT 0;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS buffer_after_minutes INTEGER DEFAULT 0;
UPDATE EventSeries SET buffer_before_minutes = 0 WHERE buffer_before_minutes IS NULL;
UPDATE EventSeries SET buffer_after_minutes = 0 WHERE buffer_after_minutes IS NULL;

ALTER TABLE Event ADD COLUMN IF NOT EXISTS provider_kind TEXT;
ALTER TABLE Event ADD COLUMN IF NOT EXISTS meeting_ref TEXT;
ALTER TABLE Event ADD COLUMN IF NOT EXISTS invitation_state TEXT;
UPDATE Event SET provider_kind = 'ExternalUrl', meeting_ref = NULLIF(TRIM(meeting_url), '') WHERE is_online = TRUE AND provider_kind IS NULL;

ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS provider_kind TEXT;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS meeting_ref TEXT;
ALTER TABLE EventSeries ADD COLUMN IF NOT EXISTS invitation_state TEXT;
UPDATE EventSeries SET provider_kind = 'ExternalUrl', meeting_ref = NULLIF(TRIM(meeting_url), '') WHERE is_online = TRUE AND provider_kind IS NULL;

ALTER TABLE EventSeriesException ADD COLUMN IF NOT EXISTS series_id INTEGER;
ALTER TABLE EventSeriesException ADD COLUMN IF NOT EXISTS occurrence_start TIMESTAMP;
ALTER TABLE EventSeriesException ADD COLUMN IF NOT EXISTS reason TEXT;

ALTER TABLE ClientNote ADD COLUMN IF NOT EXISTS linked_event_id INTEGER;
ALTER TABLE ClientNote ADD COLUMN IF NOT EXISTS linked_series_id INTEGER;
ALTER TABLE ClientNote ADD COLUMN IF NOT EXISTS linked_occurrence_start TIMESTAMP;
)duckdb";

constexpr auto kSelectScheduleIdentityBySeriesQuery = R"duckdb(
SELECT series_id, series_uid, timezone, invitation_generation, invitation_key,
       desired_revision, acked_revision, acked_content_hash, sync_state, last_error
FROM ScheduleSeries WHERE series_id = $1
)duckdb";

constexpr auto kSelectScheduleIdentityByUidQuery = R"duckdb(
SELECT series_id, series_uid, timezone, invitation_generation, invitation_key,
       desired_revision, acked_revision, acked_content_hash, sync_state, last_error
FROM ScheduleSeries WHERE series_uid = $1
)duckdb";

constexpr auto kInsertScheduleIdentityQuery = R"duckdb(
INSERT INTO ScheduleSeries (
    series_id, series_uid, timezone, invitation_generation, invitation_key,
    desired_revision, acked_revision, acked_content_hash, sync_state, last_error,
    created_at, updated_at
) VALUES ($1, $2, $3, 0, NULL, 0, 0, NULL, 'pending', NULL, $4, $4)
)duckdb";

// A new local edit always makes the series pending again, except while the
// series is paused by a conflict: only an explicit publish resumes it.
constexpr auto kBumpScheduleDesiredRevisionQuery = R"duckdb(
UPDATE ScheduleSeries
SET desired_revision = desired_revision + 1,
    sync_state = CASE WHEN sync_state = 'conflict' THEN 'conflict' ELSE 'pending' END,
    last_error = CASE WHEN sync_state = 'conflict' THEN last_error ELSE NULL END,
    updated_at = $2
WHERE series_id = $1
)duckdb";

constexpr auto kSelectScheduleOutboxQuery = R"duckdb(
SELECT series_uid, pending_payload, pending_desired_revision, inflight_revision,
       inflight_payload, inflight_hash, inflight_desired_revision
FROM ScheduleOutbox WHERE series_uid = $1
)duckdb";

constexpr auto kInsertScheduleOutboxQuery = R"duckdb(
INSERT INTO ScheduleOutbox (series_uid, pending_payload, pending_desired_revision, updated_at)
VALUES ($1, $2, $3, $4)
)duckdb";

constexpr auto kUpdateScheduleOutboxPendingQuery = R"duckdb(
UPDATE ScheduleOutbox
SET pending_payload = $2, pending_desired_revision = $3, updated_at = $4
WHERE series_uid = $1
)duckdb";

constexpr auto kFreezeScheduleOutboxQuery = R"duckdb(
UPDATE ScheduleOutbox
SET inflight_revision = $3, inflight_payload = $4, inflight_hash = $5,
    inflight_desired_revision = pending_desired_revision,
    pending_payload = NULL, pending_desired_revision = NULL, updated_at = $6
WHERE series_uid = $1
  AND inflight_payload IS NULL
  AND pending_payload IS NOT NULL
  AND pending_desired_revision = $2
)duckdb";

constexpr auto kClearScheduleInflightQuery = R"duckdb(
UPDATE ScheduleOutbox
SET inflight_revision = NULL, inflight_payload = NULL, inflight_hash = NULL,
    inflight_desired_revision = NULL, updated_at = $2
WHERE series_uid = $1
)duckdb";

constexpr auto kClearScheduleOutboxQuery = R"duckdb(
UPDATE ScheduleOutbox
SET pending_payload = NULL, pending_desired_revision = NULL,
    inflight_revision = NULL, inflight_payload = NULL, inflight_hash = NULL,
    inflight_desired_revision = NULL, updated_at = $2
WHERE series_uid = $1
)duckdb";

constexpr auto kAckScheduleIdentityQuery = R"duckdb(
UPDATE ScheduleSeries
SET acked_revision = $2, acked_content_hash = $3, sync_state = $4,
    last_error = NULL, updated_at = $5
WHERE series_uid = $1
)duckdb";

constexpr auto kSetScheduleSyncStateQuery = R"duckdb(
UPDATE ScheduleSeries
SET sync_state = $2, last_error = $3, updated_at = $4
WHERE series_uid = $1
)duckdb";

constexpr auto kAdoptScheduleServerRevisionQuery = R"duckdb(
UPDATE ScheduleSeries
SET acked_revision = $2, acked_content_hash = $3, sync_state = 'pending',
    last_error = NULL, updated_at = $4
WHERE series_uid = $1
)duckdb";

constexpr auto kSelectScheduleSeriesPendingSyncQuery = R"duckdb(
SELECT s.series_id, s.series_uid, s.timezone, s.invitation_generation, s.invitation_key,
       s.desired_revision, s.acked_revision, s.acked_content_hash, s.sync_state, s.last_error
FROM ScheduleSeries s
JOIN ScheduleOutbox o ON o.series_uid = s.series_uid
WHERE o.pending_payload IS NOT NULL OR o.inflight_payload IS NOT NULL
ORDER BY s.series_id
)duckdb";

constexpr auto kSetScheduleInvitationKeyQuery = R"duckdb(
UPDATE ScheduleSeries SET invitation_key = $2, updated_at = $3 WHERE series_id = $1
)duckdb";

constexpr auto kSetScheduleInvitationGenerationQuery = R"duckdb(
UPDATE ScheduleSeries SET invitation_generation = $2, updated_at = $3 WHERE series_id = $1
)duckdb";

constexpr auto kSelectScheduleOverridesQuery = R"duckdb(
SELECT original_occurrence_start, start_date, end_date, event_stat_id
FROM Event
WHERE series_id = $1 AND original_occurrence_start IS NOT NULL
  AND start_date IS NOT NULL AND end_date IS NOT NULL
ORDER BY original_occurrence_start
)duckdb";

constexpr auto kSelectScheduleExceptionsQuery = R"duckdb(
SELECT occurrence_start FROM EventSeriesException
WHERE series_id = $1 ORDER BY occurrence_start
)duckdb";

constexpr auto kInsertEventQuery = R"duckdb(
INSERT INTO Event (
    id,
    name, description, is_work_event,
    event_stat_id, payment_stat_id,
    start_date, end_date, duration, cost,
    is_online, meeting_url, series_id, original_occurrence_start,
    cancellation_reason, canceled_by, buffer_before_minutes, buffer_after_minutes,
    provider_kind, meeting_ref, invitation_state
)
SELECT
    COALESCE(MAX(id), 0) + 1,
    $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20
FROM Event
RETURNING id
)duckdb";

constexpr auto kUpdateEventQuery = R"duckdb(
UPDATE Event
SET name = $1,
    description = $2,
    is_work_event = $3,
    event_stat_id = COALESCE($4, event_stat_id),
    payment_stat_id = COALESCE($5, payment_stat_id),
    start_date = $6,
    end_date = $7,
    duration = $8,
    cost = $9,
    is_online = $10,
    meeting_url = $11,
    series_id = $12,
    original_occurrence_start = $13,
    cancellation_reason = $14,
    canceled_by = $15,
    buffer_before_minutes = $16,
    buffer_after_minutes = $17,
    provider_kind = $18,
    meeting_ref = $19,
    invitation_state = $20,
    reminder_notified_at = CASE
        WHEN start_date IS DISTINCT FROM $6 OR end_date IS DISTINCT FROM $7 THEN NULL
        ELSE reminder_notified_at
    END
WHERE id = $21
)duckdb";

constexpr auto kInsertEventSeriesQuery = R"duckdb(
INSERT INTO EventSeries (
    id,
    name, description, client_id, is_work_event,
    event_stat_id, payment_stat_id,
    start_date, end_date, duration, cost,
    is_online, meeting_url, recurrence_rule, recurrence_until,
    active, created_at, updated_at, cancellation_reason, canceled_by,
    buffer_before_minutes, buffer_after_minutes,
    provider_kind, meeting_ref, invitation_state
)
SELECT
    COALESCE((SELECT MAX(id) FROM EventSeries), 0) + 1,
    $1, $2, $3, $4, $5, $6, $7, $8, $9, $10,
    $11, $12, $13, $14, TRUE, $15, $15, $16, $17, $18, $19, $20, $21, $22
RETURNING id
)duckdb";

constexpr auto kSelectEventSeriesForRangeQuery = R"duckdb(
SELECT * FROM EventSeries
WHERE active = TRUE
  AND start_date <= $1
  AND (recurrence_until IS NULL OR recurrence_until >= $2)
)duckdb";

constexpr auto kSelectEventSeriesForClientAndRangeQuery = R"duckdb(
SELECT * FROM EventSeries
WHERE client_id = $1
  AND active = TRUE
  AND start_date <= $2
  AND (recurrence_until IS NULL OR recurrence_until >= $3)
)duckdb";

constexpr auto kSelectEventSeriesByIdQuery = R"duckdb(
SELECT * FROM EventSeries
WHERE id = $1
LIMIT 1
)duckdb";

constexpr auto kUpdateEventSeriesQuery = R"duckdb(
UPDATE EventSeries
SET name = $1,
    description = $2,
    client_id = $3,
    is_work_event = $4,
    event_stat_id = $5,
    payment_stat_id = $6,
    start_date = $7,
    end_date = $8,
    duration = $9,
    cost = $10,
    is_online = $11,
    meeting_url = $12,
    recurrence_rule = $13,
    recurrence_until = $14,
    updated_at = $15,
    cancellation_reason = $16,
    canceled_by = $17,
    buffer_before_minutes = $18,
    buffer_after_minutes = $19,
    provider_kind = $20,
    meeting_ref = $21,
    invitation_state = $22
WHERE id = $23
)duckdb";

constexpr auto kDeactivateEventSeriesQuery = R"duckdb(
UPDATE EventSeries
SET active = FALSE,
    updated_at = $2
WHERE id = $1
)duckdb";

constexpr auto kDeleteEventSeriesOverridesFromQuery = R"duckdb(
DELETE FROM Event
WHERE series_id = $1
  AND original_occurrence_start >= $2
)duckdb";

constexpr auto kSelectEventSeriesExceptionsForRangeQuery = R"duckdb(
SELECT series_id, occurrence_start
FROM EventSeriesException
WHERE occurrence_start >= $1 AND occurrence_start <= $2
)duckdb";

constexpr auto kInsertEventSeriesExceptionQuery = R"duckdb(
INSERT INTO EventSeriesException (id, series_id, occurrence_start, reason)
SELECT COALESCE((SELECT MAX(id) FROM EventSeriesException), 0) + 1, $1, $2, $3
WHERE NOT EXISTS (
    SELECT 1 FROM EventSeriesException
    WHERE series_id = $1 AND occurrence_start = $2
)
RETURNING id
)duckdb";

constexpr auto kSelectNotifiedSeriesOccurrencesForRangeQuery = R"duckdb(
SELECT series_id, occurrence_start
FROM EventSeriesOccurrenceReminder
WHERE occurrence_start >= $1 AND occurrence_start <= $2
)duckdb";

constexpr auto kMarkSeriesOccurrenceReminderNotifiedQuery = R"duckdb(
INSERT INTO EventSeriesOccurrenceReminder (series_id, occurrence_start, notified_at)
SELECT $1, $2, $3
WHERE NOT EXISTS (
    SELECT 1 FROM EventSeriesOccurrenceReminder
    WHERE series_id = $1 AND occurrence_start = $2
)
)duckdb";

constexpr auto kSelectMaterializedOccurrenceStartsForSeriesQuery = R"duckdb(
SELECT original_occurrence_start
FROM Event
WHERE series_id = $1 AND original_occurrence_start IS NOT NULL
)duckdb";

constexpr auto kSelectEventBySeriesOccurrenceQuery = R"duckdb(
SELECT * FROM Event
WHERE series_id = $1 AND original_occurrence_start = $2
LIMIT 1
)duckdb";

constexpr auto kInsertEventChangeLogQuery = R"duckdb(
INSERT INTO EventChangeLog (
    id, event_id, change_kind,
    old_event_stat_id, new_event_stat_id,
    old_payment_stat_id, new_payment_stat_id,
    old_start_date, new_start_date,
    cancellation_reason, occurred_at
)
SELECT COALESCE(MAX(id), 0) + 1, $1, $2, $3, $4, $5, $6, $7, $8, $9, $10
FROM EventChangeLog
RETURNING id
)duckdb";

constexpr auto kDeleteEventChangeLogByEventIdQuery =
    "DELETE FROM EventChangeLog WHERE event_id = $1";

constexpr auto kSelectEventChangeLogForClientQuery = R"duckdb(
SELECT ecl.*, e.start_date AS event_current_start_date
FROM EventChangeLog ecl
JOIN Event e ON e.id = ecl.event_id
JOIN EventClient ec ON ec.event_id = ecl.event_id
WHERE ec.client_id = $1
ORDER BY ecl.occurred_at ASC, ecl.id ASC
)duckdb";

constexpr auto kDeleteEventClientByEventIdQuery =
    "DELETE FROM EventClient WHERE event_id = $1";
constexpr auto kDeleteEventByIdQuery = "DELETE FROM Event WHERE id = $1";
constexpr auto kSelectEventByIdQuery = "SELECT * FROM Event WHERE id = $1";
constexpr auto kSelectClientIdsByEventIdQuery =
    "SELECT client_id FROM EventClient WHERE event_id = $1 ORDER BY id";

constexpr auto kInsertClientQuery = R"duckdb(
INSERT INTO Client (
    id,
    name, last_name, additional_info, diagnosis,
    birthday_date, email, phone_number, client_active,
    country, city, time_zone
)
SELECT
    COALESCE(MAX(id), 0) + 1,
    $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11
FROM Client
RETURNING id
)duckdb";

constexpr auto kUpdateClientQuery = R"duckdb(
UPDATE Client
SET name = $1,
    last_name = $2,
    additional_info = $3,
    diagnosis = $4,
    birthday_date = $5,
    email = $6,
    phone_number = $7,
    client_active = $8,
    country = $9,
    city = $10,
    time_zone = $11
WHERE id = $12
)duckdb";

constexpr auto kSelectClientByIdQuery = "SELECT * FROM Client WHERE id = $1";
constexpr auto kSelectAllClientsQuery = "SELECT * FROM Client";
constexpr auto kSelectAllClientIdsQuery = "SELECT id FROM Client";
constexpr auto kHasClientEventsQuery =
    "SELECT 1 FROM EventClient WHERE client_id = $1 LIMIT 1";
constexpr auto kDeactivateClientByIdQuery =
    "UPDATE Client SET client_active = FALSE WHERE id = $1";
constexpr auto kDeleteEventClientByClientIdQuery =
    "DELETE FROM EventClient WHERE client_id = $1";
constexpr auto kDeleteClientByIdQuery = "DELETE FROM Client WHERE id = $1";

constexpr auto kInsertEventClientQuery = R"duckdb(
INSERT INTO EventClient (id, client_id, event_id)
SELECT COALESCE(MAX(id), 0) + 1, $1, $2
FROM EventClient
RETURNING id
)duckdb";

constexpr auto kInsertClientNoteQuery = R"duckdb(
INSERT INTO ClientNote (
    id, client_id, body_markdown, created_at, updated_at,
    linked_event_id, linked_series_id, linked_occurrence_start
)
SELECT COALESCE(MAX(id), 0) + 1, $1, $2, $3, $4, $5, $6, $7
FROM ClientNote
RETURNING id
)duckdb";

constexpr auto kSelectClientNotesQuery = R"duckdb(
SELECT id, client_id, body_markdown, created_at, updated_at,
       linked_event_id, linked_series_id, linked_occurrence_start
FROM ClientNote
WHERE client_id = $1
ORDER BY created_at ASC, id ASC
)duckdb";

constexpr auto kSelectEventsForClientQuery = R"duckdb(
SELECT e.* FROM Event e
JOIN EventClient ec ON ec.event_id = e.id
WHERE ec.client_id = $1
ORDER BY e.start_date ASC
)duckdb";

constexpr auto kInsertClientNoteAttachmentQuery = R"duckdb(
INSERT INTO ClientNoteAttachment (
    id, note_id, file_name, relative_path, mime_type, size_bytes, created_at
)
SELECT COALESCE(MAX(id), 0) + 1, $1, $2, $3, $4, $5, $6
FROM ClientNoteAttachment
RETURNING id
)duckdb";

constexpr auto kSelectNoteAttachmentsQuery = R"duckdb(
SELECT id, note_id, file_name, relative_path, mime_type, size_bytes, created_at
FROM ClientNoteAttachment
WHERE note_id = $1
ORDER BY created_at ASC, id ASC
)duckdb";

constexpr auto kHasConflictQuery = R"duckdb(
SELECT id, start_date, end_date,
       COALESCE(buffer_before_minutes, 0), COALESCE(buffer_after_minutes, 0)
FROM Event
WHERE id != $1
  AND start_date IS NOT NULL
  AND end_date IS NOT NULL
)duckdb";

constexpr auto kSelectDayEventsQuery = R"duckdb(
SELECT * FROM Event
WHERE start_date <= $1 AND end_date >= $2
  AND (
    series_id IS NULL
    OR EXISTS (
      SELECT 1 FROM EventSeries
      WHERE EventSeries.id = Event.series_id
        AND EventSeries.active = TRUE
    )
  )
)duckdb";

constexpr auto kSelectUpcomingEventsQuery = R"duckdb(
SELECT * FROM Event
WHERE start_date IS NOT NULL
  AND start_date >= $1
  AND start_date <= $2
  AND event_stat_id IN (1, 2, 4)
  AND reminder_notified_at IS NULL
  AND (
    series_id IS NULL
    OR EXISTS (
      SELECT 1 FROM EventSeries
      WHERE EventSeries.id = Event.series_id
        AND EventSeries.active = TRUE
    )
  )
ORDER BY start_date ASC
)duckdb";

constexpr auto kMarkEventReminderNotifiedQuery = R"duckdb(
UPDATE Event
SET reminder_notified_at = $2
WHERE id = $1
)duckdb";

constexpr auto kSelectClientByEventQuery = R"duckdb(
SELECT c.*
FROM Client c
JOIN EventClient ec ON c.id = ec.client_id
WHERE ec.event_id = $1
)duckdb";

constexpr auto kSelectClientMonthlyStatsQuery = R"duckdb(
WITH event_stats AS (
    SELECT
        CAST(year(e.start_date) AS INTEGER) AS event_year,
        CAST(month(e.start_date) AS INTEGER) AS event_month,
        COUNT(*) AS sessions,
        COALESCE(SUM(CASE
            WHEN e.is_work_event AND e.payment_stat_id = 2 THEN COALESCE(e.cost, 0)
            ELSE 0
        END), 0) AS income
    FROM Event e
    JOIN EventClient ec ON ec.event_id = e.id
    WHERE ec.client_id = $1
      AND e.start_date IS NOT NULL
      AND e.event_stat_id IN (1, 2, 4)
      AND e.start_date >= date_trunc('month', current_timestamp) - (($2 - 1) * INTERVAL '1 month')
    GROUP BY 1, 2
)
SELECT event_year, event_month, sessions, income
FROM event_stats
ORDER BY event_year, event_month
)duckdb";

constexpr auto kSelectDashboardSummaryQuery = R"duckdb(
WITH month_start AS (
    SELECT date_trunc('month', current_timestamp) AS value
)
SELECT
    (SELECT COUNT(*) FROM Client) AS total_clients,
    (SELECT COUNT(*) FROM Client WHERE client_active = TRUE) AS active_clients,
    (SELECT COUNT(*) FROM Event e, month_start ms WHERE e.start_date >= ms.value AND e.event_stat_id IN (1, 2, 4)) AS sessions_this_month,
    (SELECT COUNT(*) FROM Event e, month_start ms WHERE e.start_date >= ms.value AND e.event_stat_id IN (1, 2, 4) AND e.is_work_event = TRUE) AS work_sessions_this_month,
    (SELECT COUNT(*) FROM Event e, month_start ms WHERE e.start_date >= ms.value AND e.event_stat_id IN (1, 2, 4) AND e.is_work_event = FALSE) AS personal_sessions_this_month,
    (SELECT COALESCE(SUM(CASE
         WHEN e.is_work_event AND e.event_stat_id IN (1, 2, 4) AND e.payment_stat_id = 2 THEN COALESCE(e.cost, 0)
         ELSE 0
     END), 0)
     FROM Event e, month_start ms
     WHERE e.start_date >= ms.value AND e.event_stat_id IN (1, 2, 4)) AS income_this_month
)duckdb";

constexpr auto kSelectDashboardMonthlyStatsQuery = R"duckdb(
SELECT
    CAST(year(e.start_date) AS INTEGER) AS event_year,
    CAST(month(e.start_date) AS INTEGER) AS event_month,
    COUNT(*) FILTER (WHERE e.event_stat_id IN (1, 2, 4)) AS sessions,
    COUNT(*) FILTER (WHERE e.event_stat_id IN (1, 2, 4) AND e.is_work_event = TRUE) AS work_sessions,
    COUNT(*) FILTER (WHERE e.event_stat_id IN (1, 2, 4) AND e.is_work_event = FALSE) AS personal_sessions,
    COALESCE(SUM(CASE
        WHEN e.is_work_event AND e.event_stat_id IN (1, 2, 4) AND e.payment_stat_id = 2 THEN COALESCE(e.cost, 0)
        ELSE 0
    END), 0) AS income
FROM Event e
WHERE e.start_date IS NOT NULL
  AND ($1 <= 0 OR e.start_date >= date_trunc('month', current_timestamp) - (($1 - 1) * INTERVAL '1 month'))
GROUP BY 1, 2
ORDER BY 1, 2
)duckdb";

constexpr auto kEventStatus = R"(
INSERT INTO EventStatus (id, name) VALUES
(1, 'scheduled'),
(2, 'completed'),
(3, 'canceled'),
(4, 'confirmed'),
(5, 'no_show'),
(6, 'rescheduled')
ON CONFLICT (id) DO UPDATE SET name = excluded.name;
)";

constexpr auto kPaymentStatus = R"(
INSERT INTO PaymentStatus (id, name) VALUES
(1, 'pending'),
(2, 'paid'),
(3, 'canceled'),
(4, 'refunded'),
(5, 'skipped')
ON CONFLICT (id) DO NOTHING;
)";

constexpr auto kDemoData = R"(
INSERT INTO Client (id, name, last_name, email, phone_number, client_active, country, city, time_zone, birthday_date, diagnosis, additional_info) VALUES
(1, 'Артём', 'Иванов', 'artem@example.com', '+79001234567', true, 'Россия', 'Москва', 'Europe/Moscow', '1990-05-15', 'Нет', 'Любит утренние тренировки'),
(2, 'Мария', 'Петрова', 'maria@example.com', '+79007654321', true, 'Россия', 'Санкт-Петербург', 'Europe/Moscow', '1985-11-22', 'Астма (в ремиссии)', 'Предпочитает онлайн-встречи'),
(3, 'Алексей', 'Сидоров', 'alex@example.com', '+79001112233', false, 'Казахстан', 'Алматы', 'Asia/Almaty', '1978-03-08', 'Гипертония', 'Занят по будням до 18:00')
ON CONFLICT (id) DO NOTHING;

INSERT INTO Event (id, name, description, is_work_event, event_stat_id, payment_stat_id, start_date, end_date, duration, cost) VALUES
(1, 'Консультация по здоровью', 'Первичная консультация', true, 2, 2, '2025-10-20 10:00:00', '2025-10-20 11:00:00', 3600, 3500.0),
(2, 'Повторный приём', 'Контрольное обследование', true, 1, 1, '2025-11-05 14:00:00', '2025-11-05 15:00:00', 3600, 2800.0),
(3, 'Отменённая сессия', 'Планировалась, но отменена', false, 3, 3, '2025-10-25 09:00:00', '2025-10-25 10:00:00', 3600, NULL),
(4, 'Групповой воркшоп', 'Йога и дыхание', false, 2, 2, '2025-10-30 18:00:00', '2025-10-30 19:30:00', 5400, NULL)
ON CONFLICT (id) DO NOTHING;

INSERT INTO EventClient (id, client_id, event_id)
SELECT 1, 1, 1 WHERE NOT EXISTS (SELECT 1 FROM EventClient WHERE client_id = 1 AND event_id = 1);

INSERT INTO EventClient (id, client_id, event_id)
SELECT 2, 1, 2 WHERE NOT EXISTS (SELECT 1 FROM EventClient WHERE client_id = 1 AND event_id = 2);

INSERT INTO EventClient (id, client_id, event_id)
SELECT 3, 2, 1 WHERE NOT EXISTS (SELECT 1 FROM EventClient WHERE client_id = 2 AND event_id = 1);

INSERT INTO EventClient (id, client_id, event_id)
SELECT 4, 2, 4 WHERE NOT EXISTS (SELECT 1 FROM EventClient WHERE client_id = 2 AND event_id = 4);

INSERT INTO EventClient (id, client_id, event_id)
SELECT 5, 3, 3 WHERE NOT EXISTS (SELECT 1 FROM EventClient WHERE client_id = 3 AND event_id = 3);
)";

constexpr auto kSelectApplicationMetadata = R"duckdb(
SELECT schema_version, backup_format_version, workspace_uuid, created_at,
       last_migration_at
FROM ApplicationMetadata
WHERE id = 1
)duckdb";

constexpr auto kInsertApplicationMetadata = R"duckdb(
INSERT INTO ApplicationMetadata (
    id, schema_version, backup_format_version, workspace_uuid, created_at,
    last_migration_at
)
SELECT 1, $1, $2, $3, $4, $4
WHERE NOT EXISTS (SELECT 1 FROM ApplicationMetadata WHERE id = 1)
)duckdb";

constexpr auto kUpdateApplicationMetadataMigrationTime = R"duckdb(
UPDATE ApplicationMetadata
SET schema_version = $1,
    backup_format_version = $2,
    last_migration_at = $3
WHERE id = 1
)duckdb";

} // namespace pcm::database::constance
