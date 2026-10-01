# Changelog

All notable changes to this project will be documented in this file.

## [0.1.35] - 2026-10-01

### Documentation

- Draft a service contract for one persistent invitation per recurring LiveKit
  series, server-side schedules and offline synchronization. Runtime support
  is not implemented in this release.

## [0.1.34] - 2026-09-26

### Added

- Internal groundwork for native LiveKit video calls: a new `src/video`
  module (`VideoProvider`/`LiveKitVideoProvider`, `VideoSession`,
  `DeviceManager`) for the in-call media session, ported from the proven
  `spike/77-livekit-cpp-spike` branch. Not yet wired into the application —
  no visible behavior changes.

## [0.1.33] - 2026-09-26

### Added

- Internal groundwork for native LiveKit video calls: `Event`/recurring
  series now carry a provider-agnostic meeting reference behind a new
  `MeetingProvider` interface. The existing "Online session" link field
  and UI keep their current behavior; the buffer-minutes fix below is the
  only behavior change in this release.

### Fixed

- Recurring sessions' configured buffer-before/buffer-after minutes are now
  correctly applied. A schema column-index bug meant these were previously
  read from the wrong columns and silently treated as zero.

## [0.1.32] - 2026-09-24

### Added

- A "Back to clients" button on the Details and Notes pages, for quicker
  navigation back to the client list.

## [0.1.31] - 2026-09-22

### Added

- Private notification modes for session reminders: Full details (previous
  behavior), Hidden (generic "Scheduled session" text, no client name or
  title), and Minimal (time only). Hidden is now the default for fresh
  installs.

## [0.1.30] - 2026-09-22

### Added

- The event editor now warns inline when the scheduled time overlaps an
  existing event (including buffer time), showing the name and time range
  of the conflicting event, with a "Suggest free slot" button that shifts
  the event to the next open slot on the same day.

## [0.1.29] - 2026-09-15

### Fixed

- The application lock no longer leaves client data visible behind the unlock
  dialog: the main window is now covered by an opaque overlay for the whole
  time the app is locked, including while it is restored from the tray.

## [0.1.28] - 2026-08-05

### Added

- Optional application lock with a six-digit PIN or 12-character password,
  Argon2id verification, idle timeout, and a manual system-tray action.
- Privacy controls to clear copied meeting links and invitations after a
  configurable delay without clearing clipboard content replaced by the user.

## [0.1.27] - 2026-08-05

### Added

- Linux releases now include an RPM package with the application binary,
  desktop entry, and icon alongside the AppImage.

## [0.1.26] - 2026-08-05

### Added

- Encrypted `.psybackup` files can now be enabled in Settings with a recovery
  password. The backup key is kept in the operating system keychain, allowing
  later manual and automatic encrypted backups without retaining the password.
- Encrypted backups are validated with their recovery password before restore
  is staged. After restart, Sessio requests the password again in
  memory to complete the restore; it is never written to the restore marker.

## [0.1.25] - 2026-08-04

### Changed

- Documented the encrypted backup format, recovery-password flow, and
  system-keychain integration before implementation.

## [0.1.23] - 2026-08-04

### Fixed

- The Russian interface no longer shows English text in the client Notes
  feed, the day-summary panel, recurring-event dialogs, and the backup and
  currency settings — translations are now kept in sync with the source
  strings, and CI blocks any future change that lets them drift again.

## [0.1.22] - 2026-08-01

### Added

- The client Notes feed now shows a small history line whenever a session's
  status, payment status, or scheduled time changes (e.g. "Status changed:
  Scheduled → Completed"), interleaved chronologically with notes and
  session cards. Click a line to jump to that session on the Calendar.

## [0.1.21] - 2026-07-31

### Fixed

- Session cost in the Notes feed now uses the app's configured currency
  (Settings → Currency) instead of the system locale's currency symbol.

## [0.1.20] - 2026-07-31

### Added

- Day summary panel on the Calendar page: session/client counts, busy
  time, next session, and the nearest free window for the selected day,
  plus a mini list of upcoming sessions that highlights the matching
  card on the timeline when clicked. Closes the empty gap between the
  calendar and Quick Slots.

## [0.1.19] - 2026-07-30

### Added

- Notes can now be linked to the session they're about, with a clickable
  badge that jumps to that event on the Calendar. Session entries in the
  timeline are clickable the same way. The feed also gained a
  jump-to-latest button.

## [0.1.18] - 2026-07-30

### Added

- Unified client timeline: the Notes screen now interleaves session
  entries with notes in one chronological feed, with an All/Sessions/Notes
  filter and a last/next-appointment summary. The same summary now also
  appears on the client card.

## [0.1.17] - 2026-07-30

### Added

- Notes journal: date-grouped feed, a client-card breadcrumb link, compact
  attachments that expand on click, and a Ctrl+Enter composer shortcut with
  a save confirmation.

## [0.1.16] - 2026-07-29

### Added

- Automatic backups: a configurable interval-based background backup with
  count-based retention, plus a shutdown safety-net check, on top of the
  existing manual backup flow. Configurable in Settings → Backup →
  Automatic Backups.

## [0.1.15] - 2026-07-29

### Fixed

- Rescheduling a single recurring occurrence to a time outside its original
  day or reminder window no longer produces a phantom duplicate Timeline
  card or reminder at the stale original slot.

## [0.1.14] - 2026-07-29

### Fixed

- Session reminder notifications now fire for upcoming recurring-series
  occurrences, not only standalone materialized events.

## [0.1.13] - 2026-07-28

### Added

- "Restore backup..." action in Settings, using a deferred apply-on-restart
  flow so the restore only runs with no open database connection.

## [0.1.12] - 2026-07-26

### Added

- `RestoreService` for validated `.psybackup` database restores.
- Staged DuckDB import, attachment restore, and protective pre-restore copies.
- Restore regression tests for database, attachments, invalid archives, and replacement safety.

## [0.1.11] - 2026-07-26

### Added

- Currency selection in Settings (₽/$/€/£), applied to the default work
  event cost field, the event editor, the timeline, and analytics.

## [0.1.10] - 2026-07-26

### Added

- "Create backup..." and "Validate backup..." actions in Settings, backed
  by the local `.psybackup` backup service.

## [0.1.9] - 2026-07-25

### Added

- Local `.psybackup` backup format: `BackupService` writes a consistent DuckDB
  snapshot (via `EXPORT DATABASE ... FORMAT PARQUET`), optional attachments,
  and a SHA-256 checksummed manifest into a zip archive, finalized atomically.
- `BackupValidator` to verify a `.psybackup` file's manifest and checksums.
- Database and attachment backup coverage in the test suite.

## [0.1.8] - 2026-07-25

### Added

- Persistent application metadata with workspace identity and schema/backup format versions.
- Database regression coverage for metadata persistence across application restarts.

## [0.1.7] - 2026-07-24

### Added

- Configurable buffers before and after events, including recurring series.
- Buffer-aware conflict checks in Timeline and DuckDB.
- Buffer-aware Quick Slots and default buffer settings.
- Russian translations for the new buffer controls.

### Fixed

- Event updates now bind the database event identifier to the correct SQL parameter.

## [0.1.6] - 2026-07-24

### Added

- event statuses for scheduled, confirmed, completed, canceled, no-show, and rescheduled sessions
- cancellation reason and cancellation initiator fields for events and recurring series
- reusable schedule conflict service with recurring-occurrence coverage

### Changed

- analytics and reminders now exclude canceled, no-show, and rescheduled sessions
- event status is kept separate from payment status
- contributor workflow now documents issue, branch, version, changelog, and MR requirements

## [0.1.5] - 2026-07-05

### Added

- recurring event series with daily, weekly, monthly, and yearly repeat options
- repeat controls in the event form, including weekday selection for weekly series
- timeline workflows for creating events from empty slots and editing recurring sessions

### Changed

- event editing now supports series-aware updates and deletion of this or future sessions
- Qlementine combobox popup sizing was patched for event dialogs
- application and release metadata were synchronized to version `0.1.5`

## [0.1.4] - 2026-07-03

### Added

- online sessions through externally managed meeting links
- meeting URL storage, opening, link copying, and invite copying from event forms
- online-session indicators and context-menu actions on timeline event cards
- configurable online-session invite template in settings

### Changed

- application and release metadata were synchronized to version `0.1.4`

## [0.1.3] - 2026-04-27

### Changed

- macOS release packaging now builds the DMG from the installed `.app` bundle
- CI now validates the packaged macOS bundle contents before publishing artifacts
- application and release metadata were synchronized to version `0.1.3`

## [0.1.2] - 2026-03-29

### Added

- client notes workspace
- quick session slots
- payment status tracking for work sessions
- session reminder settings and desktop notifications
- system tray menu with restore and full quit actions

### Changed

- income analytics now count only paid work sessions
- release packaging workflow for Linux, Windows, and macOS was polished
- Russian and English translations were expanded for payment, tray, and reminder flows

### Fixed

- updating an existing event time now persists correctly in the database
- event editing flow no longer depends on passing live `QEventItem*` outside the graphics scene
- multiple crashes around event editing, scene refresh, and tray lifecycle were fixed

## 0.1.1 - 2026-03-19

### Added
- Client notes workspace with a dedicated `Notes` page.
- Markdown note rendering in a self-chat style timeline.
- File and image attachments for notes.
- Image previews and quick file opening from notes.
- Notes entry action directly in the client list card.
- New `notes` icon and sidebar navigation support.

### Improved
- Better separation of client actions: `Details`, `Notes`, and `Delete` now work independently.
- Notes layout, bubble sizing, and long-text wrapping.
- Client list action column sizing for three actions.
- Shared widget constants for notes layout.

### Internal
- Added `ClientNote` and `ClientNoteAttachment` persistence in DuckDB.
- Attachments are stored on disk, with metadata kept in the database.
- Added `en` and `ru` translations for notes-related UI.
- Project version bumped to `0.1.1`.
