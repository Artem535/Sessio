# Calendar Month Layout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Transfer the accepted Qt calendar composition into real Sessio with a Qlementine day/month Switch.
**Architecture:** Non-mutating range projection in QTimelineModel, reusable month widget, existing event details in inspector mode. EventInfo owns selection and routes the existing editor/call services.
**Tech Stack:** C++20, Qt6 Widgets, Qlementine, DuckDB, existing recurrence and call services.
**Spec:** `docs/superpowers/specs/2026-10-02-calendar-month-layout.md`.

## Global Constraints

- Worktree only; base bc378019, issue #102, dependent video PR #94.
- Day unchecked, Month checked; actual `oclero::qlementine::Switch`, no tristate.
- Preserve day timeline, editor, quick slots, day summary, client navigation and calls.
- Month reads never mutate the day model; virtual selection uses stable occurrence identity.
- No backend/schema changes, secrets in logs, new secret caches or production deployment.
- Theme-aware Qt/Qlementine controls; no copied prototype QSS that defeats dark mode.
- Manual edits apply_patch, commands rtk; translations before commit and version 0.2.9 before runtime PR.

## Review Focus

- A moved occurrence outside its original month must appear only at its effective date.
- Day reload must not make a monthly negative identifier point at another recurrence.
- Cross-midnight/DST events appear on the correct visible dates without duplicate storage rows.
- Deleted/stale selection must not expose another event's invitation or join target.
- Dense days and a small dark-theme window retain readable, reachable controls.

## Task 1: Non-mutating month projection and calendar widget

**Files:** modify `src/event_view/qtimeline_model.{h,cpp}`; create
`src/pages/event_info_page/month_calendar_widget.{h,cpp}`; modify its CMake and
`test/CMakeLists.txt`; add `test/month_calendar_tests.cpp`.

**Interfaces:** `QVector<DuckEvent> QTimelineModel::eventsForRange(const QDate &first, const QDate &last) const`, inclusive dates, no model reset.
MonthCalendarWidget exposes `setMonth(QDate)`, `setSelectedDate(QDate)`,
`setEvents(QVector<DuckEvent>)`, `firstVisibleDate()/lastVisibleDate()` and signals
`dateSelected(QDate)`, `eventSelected(DuckEvent)`; emits data copies, not row indexes.

- [ ] Add failing real-DB tests: unchanged day rows/current selection after range read; transferred occurrence and excluded exception; midnight overlap; local DST boundary.
- [ ] Run focused tests, record actual RED.
- [ ] Extract the existing daily projection into range helper without changing its day behavior; reuse recurrence projection and client display-name resolution.
- [ ] Implement month cell sizing, locale weekday origin, adjacent dates, elided event labels/tooltips and selectable overflow list. Never silently drop dense-day events.
- [ ] Test 35/42 cells, month/year boundaries, selecting copied occurrence data and overflow selection; run focused GREEN and recurrence regressions.
- [ ] Commit and report exact APIs, tests and limitations.

## Task 2: Page composition and shared inspector

**Files:** `src/pages/event_info_page/event_info.{h,cpp}`, `qevent_details_widget.{h,cpp}`,
`ui/pages/eventinfo.ui`, `ui/pages/eventdetails.ui` as needed,
`src/app/main_window.{h,cpp}`, `ui/app/mainwindow.ui`,
`test/calendar_layout_tests.cpp`, existing relevant Qt tests/CMake.

**Consumes:** Task1 projection and month widget. Existing QTimelineWidget day loading,
QEventDetailsWidget loadEvent and setupSeriesCall.
**Produces:** QEventInfoPage exposes `setMonthView(bool)`/`isMonthView()` for tests;
shared inspector uses `setInspectorMode(bool)` and emits `editRequested()`.

- [ ] RED: actual Qlementine Switch toggles visible calendar, preserves selected date and day contents; online and ordinary event inspector have appropriate actions.
- [ ] Implement header, Switch with visible Day/Month labels and accessibility name; mode changes do not write settings/DB unless explicitly needed (session-only preference).
- [ ] Replace redundant side calendar framing with month/day working area and common inspector. Keep quick slots/day summary in day mode, accessible by scrolling.
- [ ] Add read-only inspector presentation to existing details widget; lifetime owns a QEventItem copy, never a pointer to temporary range vectors. Dialog modes remain unchanged.
- [ ] Route monthly selection to day data using persistent id or series/original key. Selection refresh clears deleted item and updates changed dates/call state.
- [ ] Route edit/create through existing dialogs and scopes; inspector invitation/open/retry reuse existing handlers and setupSeriesCall.
- [ ] Keep main navigation and client-specific controls accessible, put settings at bottom. Match accepted spacing through existing theme APIs.
- [ ] GREEN: real DB selection/update/delete, recurrence identity, online targets, legacy/external/ordinary meetings, day/month roundtrip, small layouts and dark palette regressions.
- [ ] Commit with report; no unsupported buttons or sample content in product.

## Task 3: Acceptance, translations and PR

**Files:** translation/app_ru.ts, translation/app_en.ts, CMakeLists.txt,
src/app/application.cpp, CHANGELOG.md, `docs/verification/102/README.md`;
small integration fixes only if verification identifies them.

- [ ] Build full application and tests using an isolated build and existing dependency prefixes.
- [ ] Run `cmake --build build-release --target update_translations`; finish all EN/RU entries. Verify sources attached to targets.
- [ ] Version 0.2.8 → 0.2.9 in both locations, CHANGELOG describes month view and shared inspector.
- [ ] Run serial complete relevant app/Qt tests with isolated config; baseline failures separately recorded. Do not claim existing call tests prove actual media acceptance.
- [ ] Produce populated real-app screenshots at 1500×900 and 1100×720, Day/Month and dark theme; verify Switch and selection in live Qt or equivalent instrumented Qt smoke, not just static prototype.
- [ ] Record implemented/tested/unverified, compare to accepted references; diff --check.
- [ ] Independent whole-branch review; fix important findings and re-review.
- [ ] Focused commit, push own branch, draft linked PR against video branch with tests/screenshots and `Closes #102`; attach PR to task. No merge/deploy.

## Execution state

User approved the visual prototype and requested transfer with a Qlementine
Switch. This plan is prepared for review before product implementation;
no runtime source changes have been made in the new worktree.
