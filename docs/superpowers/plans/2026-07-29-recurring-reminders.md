# Fix Recurring/Virtual Occurrence Reminders — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Session reminder notifications must fire for recurring-series occurrences, not just materialized `Event` rows, closing #36.

**Architecture:** `Database::get_upcoming_events()` only ever queried the `Event` table; recurring occurrences shown on the Timeline are generated client-side as unmaterialized "virtual occurrences" (`pcm::recurrence::occurrences`/`buildVirtualOccurrence`, `src/event_view/recurrence_utils.cpp`) and never become `Event` rows, so they were invisible to the reminder check. The fix mirrors `QTimelineModel::loadEventsForDay()`'s existing expansion pattern (materialized-event dedup + series exceptions + recurrence expansion) inside `Application::checkUpcomingEventNotifications()`, and adds a small new table to persist "already notified" state for virtual occurrences, since they have no `Event` row to stamp `reminder_notified_at` on.

**Tech Stack:** C++20, Qt6, DuckDB, GoogleTest.

## Global Constraints

- Schema changes require a migration and a restore/round-trip test (`AGENTS.md`) — the new table uses the existing additive `CREATE TABLE IF NOT EXISTS` pattern (no `schema_version` bump needed, matching every prior additive change in this repo) and gets a dedicated backup/restore round-trip test.
- Mirror the exact status filter already used by `kSelectUpcomingEventsQuery` (`event_stat_id IN (1, 2, 4)` = scheduled/completed/confirmed) when deciding which series are eligible for occurrence reminders — do not change that filter's meaning, just replicate it for series.
- No new automated tests for `src/app` (matches the existing convention — zero test coverage there in this repo); the DB-level logic gets full TDD coverage instead.
- Raise the application version for this MR: `CMakeLists.txt` and `src/app/application.cpp` both go from `0.1.13` to `0.1.14`.
- Update `CHANGELOG.md`.
- Two-space indentation, existing Qt/DuckDB naming conventions (`PascalCase` classes, `camelCase` methods, `snake_case` DB APIs).

---

### Task 1: Persist "notified" state for series occurrences

**Files:**
- Modify: `src/database/constants.hpp`
- Modify: `src/database/database.h`
- Modify: `src/database/database.cpp`
- Test: `test/database_tests.cpp`

**Interfaces:**
- Consumes: `executePrepared` (anonymous-namespace helper, `src/database/database.cpp:24`), `db_utils::toDuckTimestamp`/`toOptionalTimestampMs` (`src/database/db_utils.hpp`), the existing `get_event_series_exceptions_for_range`/`mark_event_reminder_notified` as the pattern to mirror.
- Produces: `std::set<std::pair<int64_t, int64_t>> Database::get_notified_series_occurrences_for_range(const int64_t &start_ms, const int64_t &end_ms);` — pairs are `{series_id, occurrence_start_ms}`. `bool Database::mark_series_occurrence_reminder_notified(const int64_t &series_id, const int64_t &occurrence_start_ms, const int64_t &notified_at_ms);`

- [ ] **Step 1: Write the failing tests**

Append to `test/database_tests.cpp` (after `HandlesNullBufferColumnsFromLegacyDatabase`, i.e. at the end of the file):

```cpp
TEST(DatabaseTest, TracksSeriesOccurrenceReminderNotifications) {
  pcm::config::Config conf{
      .db_conf = pcm::config::DatabaseConfig{
          .db_pth = Poco::Path(Poco::Path::current()).append("tmp_dir_series_reminder")}};

  auto db_dir = Poco::File(conf.db_conf().db_pth);
  if (db_dir.exists()) {
    db_dir.remove(true);
  }

  pcm::database::Database db{conf};
  DuckEventSeries series;
  series.name = std::string{"Weekly Session"};
  series.start_date = 1730000000000;
  series.end_date = 1740000000000;
  series.duration = 3600;
  series.recurrence_rule = "FREQ=WEEKLY;INTERVAL=1";
  const auto seriesId = db.add_event_series(series);
  ASSERT_GT(seriesId, 0);

  const int64_t occurrenceStartMs = 1730000000000;
  const int64_t notifiedAtMs = 1730000000000;

  EXPECT_TRUE(
      db.get_notified_series_occurrences_for_range(0, occurrenceStartMs + 1)
          .empty());

  EXPECT_TRUE(db.mark_series_occurrence_reminder_notified(
      seriesId, occurrenceStartMs, notifiedAtMs));

  const auto notified =
      db.get_notified_series_occurrences_for_range(0, occurrenceStartMs + 1);
  ASSERT_EQ(notified.size(), 1u);
  EXPECT_TRUE(notified.contains({seriesId, occurrenceStartMs}));

  // Marking the same occurrence again must not create a duplicate row or error.
  EXPECT_TRUE(db.mark_series_occurrence_reminder_notified(
      seriesId, occurrenceStartMs, notifiedAtMs));
  EXPECT_EQ(
      db.get_notified_series_occurrences_for_range(0, occurrenceStartMs + 1).size(),
      1u);

  db_dir.remove(true);
}
```

- [ ] **Step 2: Build and confirm the new test fails to compile**

Run: `cmake --preset vcpkg-release -DPCM_BUILD_TESTS=ON && cmake --build build-release --target PsyClientManager_database_tests --parallel`
Expected: FAIL — `get_notified_series_occurrences_for_range`/`mark_series_occurrence_reminder_notified` are not members of `Database`.

- [ ] **Step 3: Add the table to the schema**

In `src/database/constants.hpp`, inside `kCreateTables`, add after the `EventSeriesException` table (after its closing `);` at what is currently line 99):

```sql

CREATE TABLE IF NOT EXISTS EventSeriesOccurrenceReminder (
    series_id INTEGER NOT NULL REFERENCES EventSeries(id),
    occurrence_start TIMESTAMP NOT NULL,
    notified_at TIMESTAMP NOT NULL,
    PRIMARY KEY (series_id, occurrence_start)
);
```

- [ ] **Step 4: Add the two query constants**

In `src/database/constants.hpp`, add after `kInsertEventSeriesExceptionQuery` (after its closing `)duckdb";`, at what is currently line 300):

```cpp

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
```

- [ ] **Step 5: Declare the new Database methods**

In `src/database/database.h`, add after the `mark_event_reminder_notified` declaration:

```cpp
  std::set<std::pair<int64_t, int64_t>>
  get_notified_series_occurrences_for_range(const int64_t &start_ms,
                                            const int64_t &end_ms);
  bool mark_series_occurrence_reminder_notified(const int64_t &series_id,
                                                const int64_t &occurrence_start_ms,
                                                const int64_t &notified_at_ms);
```

- [ ] **Step 6: Implement the new Database methods**

In `src/database/database.cpp`, add after `Database::mark_event_reminder_notified` (after its closing `}`):

```cpp

std::set<std::pair<int64_t, int64_t>>
Database::get_notified_series_occurrences_for_range(const int64_t &start_ms,
                                                     const int64_t &end_ms) {
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kSelectNotifiedSeriesOccurrencesForRangeQuery,
      {db_utils::toDuckTimestamp(std::make_optional(start_ms * 1000)),
       db_utils::toDuckTimestamp(std::make_optional(end_ms * 1000))});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to get notified series occurrences: " << result->GetError();
    return {};
  }

  std::set<std::pair<int64_t, int64_t>> notified;
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      const auto seriesId =
          static_cast<int64_t>(chunk->GetValue(0, i).GetValue<int32_t>());
      const auto occurrenceStart =
          db_utils::toOptionalTimestampMs(chunk->GetValue(1, i)).value_or(0);
      notified.insert({seriesId, occurrenceStart});
    }
  }
  return notified;
}

bool Database::mark_series_occurrence_reminder_notified(
    const int64_t &series_id, const int64_t &occurrence_start_ms,
    const int64_t &notified_at_ms) {
  if (series_id <= 0 || occurrence_start_ms <= 0 || notified_at_ms <= 0) {
    return false;
  }

  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kMarkSeriesOccurrenceReminderNotifiedQuery,
      {duckdb::Value::BIGINT(series_id),
       db_utils::toDuckTimestamp(std::make_optional(occurrence_start_ms * 1000)),
       db_utils::toDuckTimestamp(std::make_optional(notified_at_ms * 1000))});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to mark series occurrence reminder as notified for series "
               << series_id << ": " << result->GetError();
    return false;
  }

  return true;
}
```

- [ ] **Step 7: Build and run the new test**

Run: `cmake --build build-release --target PsyClientManager_database_tests --parallel && ./build-release/test/PsyClientManager_database_tests --gtest_filter="DatabaseTest.TracksSeriesOccurrenceReminderNotifications"`
Expected: PASS.

- [ ] **Step 8: Run the full database test suite to confirm no regressions**

Run: `./build-release/test/PsyClientManager_database_tests`
Expected: PASS — all tests green.

- [ ] **Step 9: Commit**

```bash
git add src/database/constants.hpp src/database/database.h src/database/database.cpp test/database_tests.cpp
git commit -m "feat(database): persist reminder-notified state for series occurrences"
```

---

### Task 2: Restore round-trip test for the new table

**Files:**
- Test: `test/backup_tests.cpp`

**Interfaces:**
- Consumes: `makeTestDatabase` (anonymous namespace, `test/backup_tests.cpp:81`), `Database::add_event_series`, `Database::mark_series_occurrence_reminder_notified`, `Database::get_notified_series_occurrences_for_range` (Task 1), `pcm::backup::BackupService::create_backup`, `pcm::backup::RestoreService::restore_backup` (existing).

- [ ] **Step 1: Write the round-trip test**

Append to `test/backup_tests.cpp`, after `TEST(RestoreServiceTest, RejectsInvalidBackupWithoutChangingTarget)` (at the end of the file):

```cpp

TEST(RestoreServiceTest, RestoresSeriesOccurrenceReminderState) {
  auto sourceDb = makeTestDatabase("tmp_restore_reminder_source");
  DuckEventSeries series;
  series.name = std::string{"Weekly Session"};
  series.start_date = 1730000000000;
  series.end_date = 1740000000000;
  series.duration = 3600;
  series.recurrence_rule = "FREQ=WEEKLY;INTERVAL=1";
  const auto seriesId = sourceDb.add_event_series(series);
  ASSERT_GT(seriesId, 0);
  ASSERT_TRUE(sourceDb.mark_series_occurrence_reminder_notified(
      seriesId, 1730000000000, 1730000000000));

  const auto backupPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_reminder.psybackup")
                              .toString();
  if (Poco::File(backupPath).exists()) {
    Poco::File(backupPath).remove();
  }
  ASSERT_TRUE(pcm::backup::BackupService{}.create_backup(sourceDb, backupPath).ok);

  const auto targetPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_reminder_target")
                              .toString();
  if (Poco::File(targetPath).exists()) {
    Poco::File(targetPath).remove(true);
  }

  const auto restoreResult =
      pcm::backup::RestoreService{}.restore_backup(backupPath, targetPath);
  ASSERT_TRUE(restoreResult.ok) << restoreResult.error;

  pcm::config::Config targetConfig{
      .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(targetPath)}};
  pcm::database::Database restoredDb{targetConfig};
  const auto notified =
      restoredDb.get_notified_series_occurrences_for_range(0, 1730000000001);
  EXPECT_TRUE(notified.contains({seriesId, 1730000000000}));

  Poco::File(backupPath).remove();
  Poco::File(targetPath).remove(true);
  Poco::File(Poco::Path(Poco::Path::current()).append("tmp_restore_reminder_source"))
      .remove(true);
}
```

- [ ] **Step 2: Build and run**

Run: `cmake --build build-release --target PsyClientManager_backup_tests --parallel && ./build-release/test/PsyClientManager_backup_tests --gtest_filter="RestoreServiceTest.RestoresSeriesOccurrenceReminderState"`
Expected: PASS.

- [ ] **Step 3: Run the full backup test suite to confirm no regressions**

Run: `./build-release/test/PsyClientManager_backup_tests`
Expected: PASS — all tests green.

- [ ] **Step 4: Commit**

```bash
git add test/backup_tests.cpp
git commit -m "test(backup): cover series occurrence reminder state in restore round-trip"
```

---

### Task 3: Expand virtual occurrences in the reminder check

**Files:**
- Modify: `src/app/application.h`
- Modify: `src/app/application.cpp`

**Interfaces:**
- Consumes: `pcm::recurrence::occurrences`/`buildVirtualOccurrence` (`src/event_view/recurrence_utils.h`, already linked into `src/app` via the existing `QTimelineModel` usage), `Database::get_day_events`, `Database::get_event_series_exceptions_for_range`, `Database::get_event_series_for_range`, `Database::get_notified_series_occurrences_for_range`, `Database::mark_series_occurrence_reminder_notified` (Task 1), `Database::get_client` (existing).
- Produces: `void Application::notifyUpcomingSeriesOccurrences(int64_t nowMs, int64_t windowEndMs);` (private helper, not a slot).

- [ ] **Step 1: Add the include**

In `src/app/application.cpp`, add to the include block (after `#include "../widgets/app_settings.h"`):

```cpp
#include "../event_view/recurrence_utils.h"
```

- [ ] **Step 2: Declare the new private method**

In `src/app/application.h`, add after the `initializeNotifications();` declaration:

```cpp
  void notifyUpcomingSeriesOccurrences(int64_t nowMs, int64_t windowEndMs);
```

- [ ] **Step 3: Call it from the existing reminder check**

In `src/app/application.cpp`, change `Application::checkUpcomingEventNotifications()`'s body from:

```cpp
  const auto events = mDb->get_upcoming_events(nowMs, windowEndMs);
  for (const auto &event : events) {
    mTrayIcon->showMessage(notificationTitleForEvent(event),
                           notificationBodyForEvent(event),
                           QSystemTrayIcon::Information, 15'000);
    mDb->mark_event_reminder_notified(event.id, nowMs);
  }
}
```

to:

```cpp
  const auto events = mDb->get_upcoming_events(nowMs, windowEndMs);
  for (const auto &event : events) {
    mTrayIcon->showMessage(notificationTitleForEvent(event),
                           notificationBodyForEvent(event),
                           QSystemTrayIcon::Information, 15'000);
    mDb->mark_event_reminder_notified(event.id, nowMs);
  }

  notifyUpcomingSeriesOccurrences(nowMs, windowEndMs);
}
```

- [ ] **Step 4: Implement `notifyUpcomingSeriesOccurrences`**

In `src/app/application.cpp`, add after `Application::checkUpcomingEventNotifications()` (after its closing `}`):

```cpp

void Application::notifyUpcomingSeriesOccurrences(const int64_t nowMs,
                                                   const int64_t windowEndMs) {
  const auto localTz = QTimeZone::systemTimeZone();
  const auto rangeStart =
      QDateTime::fromMSecsSinceEpoch(nowMs, QTimeZone::UTC).toTimeZone(localTz);
  const auto rangeEnd =
      QDateTime::fromMSecsSinceEpoch(windowEndMs, QTimeZone::UTC).toTimeZone(localTz);

  // Mirrors the status filter used by kSelectUpcomingEventsQuery: scheduled(1),
  // completed(2), confirmed(4). Excludes canceled(3), no_show(5), rescheduled(6).
  const auto isEligibleStatus = [](const int64_t statusId) {
    return statusId == 1 || statusId == 2 || statusId == 4;
  };

  const auto materialized = mDb->get_day_events(nowMs, windowEndMs);
  std::set<std::pair<int64_t, int64_t>> materializedOccurrences;
  for (const auto &event : materialized) {
    if (event.series_id.has_value() && event.original_occurrence_start.has_value()) {
      materializedOccurrences.insert(
          {*event.series_id, *event.original_occurrence_start});
    }
  }

  const auto exceptions =
      mDb->get_event_series_exceptions_for_range(nowMs, windowEndMs);
  const auto alreadyNotified =
      mDb->get_notified_series_occurrences_for_range(nowMs, windowEndMs);

  auto seriesList = mDb->get_event_series_for_range(nowMs, windowEndMs);
  for (auto &series : seriesList) {
    if (!isEligibleStatus(series.event_stat_id)) {
      continue;
    }
    if (series.is_work_event && series.client_id.has_value()) {
      try {
        const auto client = mDb->get_client(*series.client_id);
        if (client) {
          const auto firstName = QString::fromStdString(client->name.value_or(""));
          const auto lastName = QString::fromStdString(client->last_name.value_or(""));
          const auto fullName = QString("%1 %2").arg(firstName, lastName).trimmed();
          if (!fullName.isEmpty()) {
            series.client_name = fullName.toStdString();
          }
        }
      } catch (const std::exception &) {
      }
    }

    const auto occurrences = pcm::recurrence::occurrences(series, rangeStart, rangeEnd);
    for (const auto &occurrence : occurrences) {
      const auto occurrenceStartMs = occurrence.toUTC().toMSecsSinceEpoch();
      const std::pair<int64_t, int64_t> key{series.id, occurrenceStartMs};
      if (exceptions.contains(key) || materializedOccurrences.contains(key) ||
          alreadyNotified.contains(key)) {
        continue;
      }

      const auto virtualEvent =
          pcm::recurrence::buildVirtualOccurrence(series, occurrence, 0);
      mTrayIcon->showMessage(notificationTitleForEvent(virtualEvent),
                             notificationBodyForEvent(virtualEvent),
                             QSystemTrayIcon::Information, 15'000);
      mDb->mark_series_occurrence_reminder_notified(series.id, occurrenceStartMs,
                                                     nowMs);
    }
  }
}
```

- [ ] **Step 5: Prefer pre-resolved client names in the notification body**

`notificationBodyForEvent` currently always calls `mDb->get_client_by_event(event.id)`, which only works for materialized events (virtual occurrences have a placeholder id with no `EventClient` row). In `src/app/application.cpp`, change `Application::notificationBodyForEvent`'s client-resolution block from:

```cpp
  QString clientText;
  if (event.is_work_event) {
    try {
      const auto client = mDb->get_client_by_event(event.id);
      const auto firstName = QString::fromStdString(client.name.value_or(""));
      const auto lastName = QString::fromStdString(client.last_name.value_or(""));
      const auto fullName = QString("%1 %2").arg(firstName, lastName).trimmed();
      if (!fullName.isEmpty()) {
        clientText = tr("Client: %1").arg(fullName);
      }
    } catch (const std::exception &) {
    }
  }
```

to:

```cpp
  QString clientText;
  if (event.is_work_event) {
    if (event.client_name.has_value()) {
      const auto fullName = QString::fromStdString(*event.client_name).trimmed();
      if (!fullName.isEmpty()) {
        clientText = tr("Client: %1").arg(fullName);
      }
    } else {
      try {
        const auto client = mDb->get_client_by_event(event.id);
        const auto firstName = QString::fromStdString(client.name.value_or(""));
        const auto lastName = QString::fromStdString(client.last_name.value_or(""));
        const auto fullName = QString("%1 %2").arg(firstName, lastName).trimmed();
        if (!fullName.isEmpty()) {
          clientText = tr("Client: %1").arg(fullName);
        }
      } catch (const std::exception &) {
      }
    }
  }
```

`event.client_name` is `std::nullopt` for every existing materialized-event call site (the raw-row `DuckEvent` constructor never populates it), so this is a no-op for the pre-existing path and only takes effect for virtual occurrences, which have it pre-populated via `buildVirtualOccurrence`.

- [ ] **Step 6: Build the app target**

Run: `cmake --build build-release --target PsyClientManager --parallel`
Expected: builds cleanly with no errors.

- [ ] **Step 7: Manual verification**

Run: `./build-release/PsyClientManager`
1. Create a recurring work-session series (e.g. weekly) with a linked client, starting within the next `notificationLeadMinutes` (Settings → Events → "Notify before start"), and with no materialized exception for that occurrence.
2. Wait for the notification poll interval (30s, `kNotificationPollIntervalMs` in `application.cpp`) and confirm a desktop notification appears for the upcoming recurring occurrence, including the client's name.
3. Confirm the same occurrence is not notified a second time on the next poll.
4. Confirm a standalone (non-recurring) event still gets exactly one reminder, unchanged from before.

Expected: all 4 steps behave as described.

- [ ] **Step 8: Commit**

```bash
git add src/app/application.h src/app/application.cpp
git commit -m "fix(app): notify reminders for recurring/virtual event occurrences"
```

---

### Task 4: Version bump and changelog

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/app/application.cpp`
- Modify: `CHANGELOG.md`

**Interfaces:** none (metadata-only changes).

- [ ] **Step 1: Bump the project version**

In `CMakeLists.txt`, change:

```cmake
project(PsyClientManager VERSION 0.1.13 LANGUAGES CXX)
```

to:

```cmake
project(PsyClientManager VERSION 0.1.14 LANGUAGES CXX)
```

- [ ] **Step 2: Bump the application version string**

In `src/app/application.cpp`, change:

```cpp
  app.setApplicationVersion("0.1.13");
```

to:

```cpp
  app.setApplicationVersion("0.1.14");
```

- [ ] **Step 3: Add the changelog entry**

In `CHANGELOG.md`, insert a new section above `## [0.1.13] - 2026-07-28`:

```markdown
## [0.1.14] - 2026-07-29

### Fixed

- Session reminder notifications now fire for upcoming recurring-series
  occurrences, not only standalone materialized events.
```

- [ ] **Step 4: Reconfigure so CMake picks up the new project version**

Run: `cmake -S . -B build-release`
Expected: reconfigures without errors.

- [ ] **Step 5: Full rebuild and full test suite**

Run: `cmake --build build-release --parallel && ctest --test-dir build-release --output-on-failure`
Expected: all targets build, all tests pass (including the new `DatabaseTest.TracksSeriesOccurrenceReminderNotifications` and `RestoreServiceTest.RestoresSeriesOccurrenceReminderState`).

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/app/application.cpp CHANGELOG.md
git commit -m "chore: bump version to 0.1.14"
```
