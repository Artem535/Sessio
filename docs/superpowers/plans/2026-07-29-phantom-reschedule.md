# Phantom Reminders on Rescheduled Occurrences Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop `QTimelineModel::loadEventsForDay()` and `Application::notifyUpcomingSeriesOccurrences()` from regenerating a phantom virtual occurrence for a recurring slot that has already been materialized-and-rescheduled to a time outside the window being queried.

**Architecture:** Add a new `Database` lookup, `get_materialized_occurrence_starts_for_series(series_id)`, that finds every `original_occurrence_start` a series has ever materialized an override for — regardless of that override's *current* `start_date`/`end_date` — by querying the `Event` table filtered only by `series_id`, with no date-range bound. Replace the window-bounded `get_day_events`-derived `materializedOccurrences` dedup set in both consumers with a per-series call to this new method.

**Tech Stack:** C++20, Qt6, DuckDB (via the existing `executePrepared`/`db_utils` helpers), GoogleTest.

## Global Constraints

- Version bump required in every MR: `CMakeLists.txt` (`project(... VERSION ...)`) and `src/app/application.cpp` (`app.setApplicationVersion(...)`) — bump 0.1.14 → 0.1.15.
- CHANGELOG.md entry required every MR (`AGENTS.md`).
- Database schema changes require a migration and a restore/round-trip test — not applicable here, this task adds no schema change, only a new query against the existing `Event` table.
- Two-space indent, PascalCase classes, camelCase Qt methods, snake_case DB APIs (`AGENTS.md`).
- Do not develop directly on `main` — this plan executes on `feat/38-phantom-reschedule` (already created off `main`).
- Out of scope (per issue #38): changing how single-occurrence edits are saved. Materializing via `addEvent()` without an `EventSeriesException` row stays as-is — the fix is in how overrides are *found*, not how they're *written*.

---

### Task 1: New `Database::get_materialized_occurrence_starts_for_series` method

**Files:**
- Modify: `src/database/constants.hpp` (add query constant after `kMarkSeriesOccurrenceReminderNotifiedQuery`, line 322)
- Modify: `src/database/database.h` (add declaration after `mark_series_occurrence_reminder_notified`, line 99)
- Modify: `src/database/database.cpp` (add implementation after `Database::mark_series_occurrence_reminder_notified`, ~line 1010)
- Test: `test/database_tests.cpp` (add test after `TracksSeriesOccurrenceReminderNotifications`, line 365)

**Interfaces:**
- Produces: `std::set<int64_t> Database::get_materialized_occurrence_starts_for_series(const int64_t &series_id)` — returns the set of `original_occurrence_start` millisecond timestamps for every `Event` row belonging to `series_id`, independent of that row's current `start_date`/`end_date`. Used by Task 2 and Task 3.

- [ ] **Step 1: Write the failing test**

Add to `test/database_tests.cpp`, immediately after the existing `TracksSeriesOccurrenceReminderNotifications` test (after line 365, before `int main`):

```cpp
TEST(DatabaseTest, FindsMaterializedOccurrenceRegardlessOfCurrentWindow) {
  pcm::config::Config conf{
      .db_conf = pcm::config::DatabaseConfig{
          .db_pth = Poco::Path(Poco::Path::current()).append("tmp_dir_materialized_override")}};

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

  EXPECT_TRUE(
      db.get_materialized_occurrence_starts_for_series(seriesId).empty());

  // Reschedule a single occurrence far outside its original slot: the
  // override's original_occurrence_start stays pinned to the old slot, but
  // start_date/end_date move to a time weeks away.
  const int64_t originalOccurrenceStartMs = 1730000000000;
  DuckEvent override;
  override.name = std::string{"Weekly Session"};
  override.series_id = seriesId;
  override.original_occurrence_start = originalOccurrenceStartMs;
  override.start_date = 1735000000000;
  override.end_date = 1735003600000;
  const auto overrideId = db.add_event(override);
  ASSERT_GT(overrideId, 0);

  const auto materializedStarts =
      db.get_materialized_occurrence_starts_for_series(seriesId);
  ASSERT_EQ(materializedStarts.size(), 1u);
  EXPECT_TRUE(materializedStarts.contains(originalOccurrenceStartMs));

  db_dir.remove(true);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-release --target unit_tests --parallel && ./build-release/test/unit_tests --gtest_filter=DatabaseTest.FindsMaterializedOccurrenceRegardlessOfCurrentWindow`
Expected: FAIL to compile — `get_materialized_occurrence_starts_for_series` is not a member of `Database`.

- [ ] **Step 3: Add the query constant**

In `src/database/constants.hpp`, immediately after `kMarkSeriesOccurrenceReminderNotifiedQuery` (after the closing `)duckdb";` on line 322), insert:

```cpp
constexpr auto kSelectMaterializedOccurrenceStartsForSeriesQuery = R"duckdb(
SELECT original_occurrence_start
FROM Event
WHERE series_id = $1 AND original_occurrence_start IS NOT NULL
)duckdb";
```

- [ ] **Step 4: Declare the method**

In `src/database/database.h`, immediately after the `mark_series_occurrence_reminder_notified` declaration (line 99), insert:

```cpp
  std::set<int64_t>
  get_materialized_occurrence_starts_for_series(const int64_t &series_id);
```

- [ ] **Step 5: Implement the method**

In `src/database/database.cpp`, immediately after the closing brace of `Database::mark_series_occurrence_reminder_notified` (after line ~1010), insert:

```cpp
std::set<int64_t> Database::get_materialized_occurrence_starts_for_series(
    const int64_t &series_id) {
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(
      conn, constance::kSelectMaterializedOccurrenceStartsForSeriesQuery,
      {duckdb::Value::BIGINT(series_id)});

  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to get materialized occurrence starts for series "
               << series_id << ": " << result->GetError();
    return {};
  }

  std::set<int64_t> starts;
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) {
      const auto occurrenceStart =
          db_utils::toOptionalTimestampMs(chunk->GetValue(0, i)).value_or(0);
      starts.insert(occurrenceStart);
    }
  }
  return starts;
}
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build build-release --target unit_tests --parallel && ./build-release/test/unit_tests --gtest_filter=DatabaseTest.FindsMaterializedOccurrenceRegardlessOfCurrentWindow`
Expected: PASS

- [ ] **Step 7: Run full test suite**

Run: `ctest --test-dir build-release --output-on-failure`
Expected: all tests pass (no regressions).

- [ ] **Step 8: Commit**

```bash
git add src/database/constants.hpp src/database/database.h src/database/database.cpp test/database_tests.cpp
git commit -m "feat(database): add unbounded materialized-occurrence lookup by series"
```

---

### Task 2: Wire the new lookup into `QTimelineModel::loadEventsForDay()`

**Files:**
- Modify: `src/event_view/qtimeline_model.cpp:78-139` (`loadEventsForDay`)

**Interfaces:**
- Consumes: `std::set<int64_t> Database::get_materialized_occurrence_starts_for_series(const int64_t &series_id)` (Task 1).

- [ ] **Step 1: Replace the window-bounded dedup set with the per-series lookup**

In `src/event_view/qtimeline_model.cpp`, replace the current `loadEventsForDay` body (lines 89-129) — which builds `materializedOccurrences` from `get_day_events` results and checks it inside the occurrence loop — with:

```cpp
  const auto events = mDb->get_day_events(dayStartMs, dayEndMs);
  mEvents = std::move(QVector<DuckEvent>(events.begin(), events.end()));
  for (auto &event : mEvents) {
    if (!event.is_work_event) {
      continue;
    }

    try {
      const auto client = mDb->get_client_by_event(event.id);
      const auto displayName = pcm::recurrence::fullClientName(client);
      if (!displayName.isEmpty()) {
        event.client_name = displayName.toStdString();
      }
    } catch (const std::exception &) {
      event.client_name = std::nullopt;
    }
  }

  const auto rangeStart = QDateTime(date, QTime(0, 0), localTz);
  const auto rangeEnd = QDateTime(date.addDays(1), QTime(0, 0), localTz).addMSecs(-1);
  const auto exceptions = mDb->get_event_series_exceptions_for_range(dayStartMs, dayEndMs);
  auto seriesList = mDb->get_event_series_for_range(dayStartMs, dayEndMs);
  for (auto &series : seriesList) {
    pcm::recurrence::resolveSeriesClientName(*mDb, series);

    const auto materializedStarts =
        mDb->get_materialized_occurrence_starts_for_series(series.id);
    const auto occurrences = pcm::recurrence::occurrences(series, rangeStart, rangeEnd);
    for (const auto &occurrence : occurrences) {
      const auto occurrenceStartMs = occurrence.toUTC().toMSecsSinceEpoch();
      if (exceptions.contains({series.id, occurrenceStartMs}) ||
          materializedStarts.contains(occurrenceStartMs)) {
        continue;
      }
      const auto virtualId =
          -(series.id * 1'000'000LL + static_cast<int64_t>(occurrence.date().toJulianDay()));
      mEvents.append(pcm::recurrence::buildVirtualOccurrence(series, occurrence, virtualId));
    }
  }
```

The rest of the function (the `std::sort`, `qDebug`, `endResetModel()`, `emit eventsLoaded()` at lines 131-138) is unchanged.

- [ ] **Step 2: Build**

Run: `cmake --build build-release --target PsyClientManager --parallel`
Expected: builds cleanly, no warnings about unused `std::pair`/`<set>` (the file still uses `std::set<int64_t>` via the new method's return type, so the `<set>` include stays needed).

- [ ] **Step 3: Run full test suite**

Run: `ctest --test-dir build-release --output-on-failure`
Expected: all tests pass (no existing test exercises `QTimelineModel` directly, so this step is a regression guard on the rest of the suite).

- [ ] **Step 4: Commit**

```bash
git add src/event_view/qtimeline_model.cpp
git commit -m "fix(timeline): find materialized recurring overrides regardless of current day"
```

---

### Task 3: Wire the new lookup into `Application::notifyUpcomingSeriesOccurrences()`

**Files:**
- Modify: `src/app/application.cpp:328-381` (`notifyUpcomingSeriesOccurrences`)

**Interfaces:**
- Consumes: `std::set<int64_t> Database::get_materialized_occurrence_starts_for_series(const int64_t &series_id)` (Task 1).

- [ ] **Step 1: Replace the window-bounded dedup set with the per-series lookup**

In `src/app/application.cpp`, replace the current `notifyUpcomingSeriesOccurrences` body (lines 342-380) with:

```cpp
  const auto exceptions =
      mDb->get_event_series_exceptions_for_range(nowMs, windowEndMs);
  const auto alreadyNotified =
      mDb->get_notified_series_occurrences_for_range(nowMs, windowEndMs);

  auto seriesList = mDb->get_event_series_for_range(nowMs, windowEndMs);
  for (auto &series : seriesList) {
    if (!isEligibleStatus(series.event_stat_id)) {
      continue;
    }
    pcm::recurrence::resolveSeriesClientName(*mDb, series);

    const auto materializedStarts =
        mDb->get_materialized_occurrence_starts_for_series(series.id);
    const auto occurrences = pcm::recurrence::occurrences(series, rangeStart, rangeEnd);
    for (const auto &occurrence : occurrences) {
      const auto occurrenceStartMs = occurrence.toUTC().toMSecsSinceEpoch();
      const std::pair<int64_t, int64_t> key{series.id, occurrenceStartMs};
      if (exceptions.contains(key) || materializedStarts.contains(occurrenceStartMs) ||
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

This removes the `materialized`/`materializedOccurrences` construction (old lines 342-349) entirely — the surrounding `rangeStart`/`rangeEnd`/`isEligibleStatus` setup (lines 328-341) is unchanged.

- [ ] **Step 2: Build**

Run: `cmake --build build-release --target PsyClientManager --parallel`
Expected: builds cleanly.

- [ ] **Step 3: Run full test suite**

Run: `ctest --test-dir build-release --output-on-failure`
Expected: all tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/app/application.cpp
git commit -m "fix(reminders): find materialized recurring overrides regardless of reminder window"
```

---

### Task 4: Version bump and CHANGELOG

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/app/application.cpp`
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Bump the version**

In `CMakeLists.txt`, change:
```cmake
project(PsyClientManager VERSION 0.1.14 LANGUAGES CXX)
```
to:
```cmake
project(PsyClientManager VERSION 0.1.15 LANGUAGES CXX)
```

In `src/app/application.cpp`, change:
```cpp
  app.setApplicationVersion("0.1.14");
```
to:
```cpp
  app.setApplicationVersion("0.1.15");
```

- [ ] **Step 2: Add the CHANGELOG entry**

In `CHANGELOG.md`, add a new section above the `[0.1.14]` entry:

```markdown
## [0.1.15] - 2026-07-29

### Fixed

- Rescheduling a single recurring occurrence to a time outside its original
  day or reminder window no longer produces a phantom duplicate Timeline
  card or reminder at the stale original slot.
```

- [ ] **Step 3: Build and run full test suite**

Run: `cmake --build build-release --parallel && ctest --test-dir build-release --output-on-failure`
Expected: builds cleanly, all tests pass.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt src/app/application.cpp CHANGELOG.md
git commit -m "chore: bump version to 0.1.15"
```
