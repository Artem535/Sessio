# "My day" («Мой день») Home Screen Design

## Intent

A specialist opens Sessio in the morning (or between sessions) and wants to see
the day at a glance: what is on, what comes next, how much the day brings in,
and what is left hanging (unpaid sessions, sessions without notes, small to-dos).
The model is the "My day" view of a phone calendar app: a vertical timeline of
today grouped into morning / afternoon / evening, past items dimmed, the
current or next one highlighted, and a link icon on online meetings. Sessio
adds a row of stat tiles on top and a to-do column on the right.

"My day" is a **separate screen**, not a mode of the Calendar. It is
read-and-jump for meetings (it never edits a meeting), and it owns one new
thing: day to-dos.

Approved mockup: `.superpowers/brainstorm/378319-1793192062/content/my-day-page.html`,
option A (stat tiles from `my-day-stats.html`, option C).

## Goals

- One glance answers: how many meetings today, when is the next one, what the
  day earns, what is unpaid, which sessions still need notes, what I must do.
- Jump from any item to the place where it is handled (Calendar, Calls tab).
- Simple day to-dos that carry over until done.
- It becomes the start page of the app.

## Non-goals

- Editing meetings here (create/move/cancel stays in the Calendar).
- Charts or period analytics (that is the Analytics page).
- A full task manager: no priorities, due times, reminders, recurrence, or
  a separate "Tasks" tab. A to-do is text + checkbox + day (+ optional links).

## Anti-goals

- The timeline must never show different data from the Calendar for the same
  day. It reads through the same `QTimelineModel` / recurrence projection.
- Opening "My day" must not change what the Calendar page shows (no calling
  `QTimelineModel::loadEventsForDay`, which moves the Calendar's day).
- Not a second Calendar: no day grid, no drag, no create-meeting button.

## Decisions made with the user

| Topic | Decision |
|---|---|
| Separate screen | Yes, its own tab, not a Calendar mode. |
| Top block | Four tiles (mockup option C): meetings today + next; today's planned income; unpaid sessions; sessions without notes. |
| Layout | Timeline on the left, "Дела на сегодня" to-do column on the right (mockup option A). |
| To-dos | Plain to-do: text + checkbox. Belongs to a day. Optional link to a client and/or a meeting. Column groups by client or by meeting via a switch. |
| Carry-over | An unfinished to-do moves forward: it shows on today until done. |
| Start page | "My day" is the first tab and is shown on launch. |
| Day navigation | `‹ Сегодня ›` arrows in the header browse previous/next days. |
| Technology | Build the screen in QML. |

## Screen

### Header

`Мой день` + the full date (e.g. "среда, 28 октября"), and on the right `‹`,
`Сегодня`, `›`. `Сегодня` is disabled when today is shown. All tiles,
timeline and to-dos follow the selected day, except where stated below.

### Stat tiles (one row of four)

"Active" event = `event_stat_id IN (1, 2, 4)` (scheduled, completed, confirmed),
the same set Analytics uses (`kSelectDashboardSummaryQuery`).

All four tiles are computed in the bridge from **projected** events
(`QTimelineModel::eventsForRange`, real rows + virtual series occurrences),
not from raw SQL over `Event`, so they always match the timeline. A past
occurrence that was never touched is still virtual, and it is exactly the
kind that is unpaid and has no note, so SQL over `Event` alone would miss it.
"Ended" = `end_date < now`, local time, with `now` from an injectable clock.

1. **Meetings** — count of active events on the day, work and personal
   (everything the timeline shows except canceled); subtitle "следующая через
   25 мин" / "следующая в 15:30" for today, "на сегодня всё" when none is
   left, the first meeting's time for a future day, nothing extra for a past day.
2. **Planned income** — sum of `cost` of active *work* events on the day,
   in the currency from `pcm::widgets::settings::currencySymbol()`.
3. **Unpaid** — not tied to the selected day: active work events that ended
   within the last 90 days with `payment_stat_id = 1` (pending); shows count
   and sum. Orange when > 0, neutral with a check mark when 0.
   *(My call: 90 days bounds the series projection; older debts are rare and
   still visible in the Calendar.)*
4. **Without notes** — not tied to the selected day: active work events that
   ended in the last 7 days with no `ClientNote` linked to them, either by
   `linked_event_id = event.id` or by `linked_series_id` +
   `linked_occurrence_start` for an occurrence. Notes saved from the call
   notes panel are `ClientNote` rows, so they count. A note the user wrote
   without linking it to the session does not count (accepted).
   Orange when > 0, neutral when 0.

Tiles 3 and 4 are clickable (see "Navigation").

### Timeline (left, wider column)

- Today's events from the shared timeline projection, including virtual
  occurrences of series, ordered by start.
- Groups: Утро (before 12:00), После полудня (12:00–17:59), Вечер (18:00+).
  Empty groups are hidden. Groups are collapsible.
- Each card: time on the left, title (client name for work events, event
  name otherwise), time range, "очно"/"личное" hint, link icon for online
  events (`is_online` or a LiveKit `provider_kind`).
- Past events (end < now) are dimmed. The current event (now within it) or,
  if none, the next one is highlighted: time pill in accent colour and an
  outline. Canceled events show dimmed with the "Отменено" prefix.
- Small tags on cards: "не оплачено" (past, work, pending), "без заметки"
  (past, work, no linked note).
- The highlighted online event shows a `Подключиться` button.
- Empty day: a short "Встреч нет" line.
- An event crossing midnight shows on every day it overlaps (same as the
  Calendar's projection).
- Updates: rebuild everything when the page is shown (it is a separate tab,
  so any Calendar/payment edit is picked up on return), when the timeline
  model signals changes while visible, after every to-do edit, and on a
  minute timer (for "через N мин", dimming and the highlight).

### To-do column (right)

- Title "Дела на сегодня · done/total" and a two-way switch
  `Клиенты | Встречи` (choice remembered in settings).
- **By clients:** groups per client; to-dos linked only to a meeting are
  grouped under that meeting's client; unlinked ones go to "Общие".
- **By meetings:** groups per meeting in time order, headed "15:30 Анна К.";
  to-dos linked only to a client, and unlinked ones, go to "Общие".
- Each row: checkbox, text, and for carried-over items a muted
  "с 26 окт." label. Checking strikes it through and keeps it in place until
  the day changes. Double-click edits the text; a delete action on hover.
- `+ Добавить дело`: inline text field plus optional client and meeting
  pickers (meeting picker lists the selected day's meetings; picking a
  meeting fills the client). Enter saves.
- Which to-dos a day shows:
  - **today**: to-dos of today, plus every unfinished to-do from earlier days
    (carry-over), plus to-dos finished today.
  - **past day**: to-dos finished that day, plus to-dos dated that day that
    are still unfinished. No carry-over into the past. (A to-do dated Mon and
    finished Wed shows on Wed only, never twice.)
  - **future day**: to-dos dated that day. Adding a to-do on a future day
    dates it that day.
  - "Finished that day" compares the local date of `done_at`.
  - The "done/total" counter counts exactly the rows shown.
- A to-do whose linked client was deleted loses the link and moves to
  "Общие". The client picker lists active clients only.

## Navigation (read-and-jump)

- Click a timeline card → switch to the Calendar tab and select that event
  without opening the editor: `QEventInfoPage::showEventOnDay(eventId, dayMs)`.
  Pass the `DuckEvent.id` the projection returned; for virtual occurrences
  that is the encoded virtual id the Calendar's own timeline uses
  (`recurrence_utils` `DecodedVirtualId`), so no new Calendar entry point
  is needed. Verify this with a test on a series occurrence.
- `Подключиться` → the same routing as the Calendar inspector's "open meeting"
  (`QEventDetailsWidget::onOpenMeetingClicked`): series-backed meetings use
  the series join target (`mSeriesJoinTarget`, built from the series
  invitation), LiveKit meetings go through
  `openLiveKitMeetingRequested(meetingRef)` → Calls tab, other links through
  `pcm::meeting::openMeetingUrl`. Extract the whole decision, series case
  included, into a shared helper used by both the inspector and "My day".
- Click "Unpaid" or "Without notes" tile → a popup list inside "My day" of
  the matching events (date, time, client, sum). Clicking a row jumps to the
  Calendar as above. *(My call — Analytics has no per-event list or filter,
  so "open Analytics with a filter" would mean building one there.)*
- Click a to-do's client/meeting label → client card / Calendar event.

## Data

New table (added to `kCreateTables` and, for existing workspaces, to
`kSchemaMigrations` with `CREATE TABLE IF NOT EXISTS`):

```
DayTodo(
  id INTEGER PRIMARY KEY (sequence like other tables),
  title VARCHAR NOT NULL,
  day DATE NOT NULL,              -- the day it was created for
  client_id INTEGER NULL,         -- no FK, matches Transcript style
  event_id INTEGER NULL,          -- real Event row
  series_id INTEGER NULL,         -- for a virtual occurrence:
  occurrence_start TIMESTAMP NULL --   series + occurrence start
  done_at TIMESTAMP NULL,
  created_at TIMESTAMP NOT NULL
)
```

- Meeting links mirror `ClientNote`'s `linked_event_id` /
  `linked_series_id` + `linked_occurrence_start`. When an occurrence is
  materialised into a real `Event`, re-point to-dos the same way (and in the
  same place) that `ClientNote` links are handled; if notes are not
  re-pointed today, match a to-do by series + occurrence start as well when
  grouping.
- When a linked event is deleted, its to-dos keep their text and lose the
  link (`event_id = NULL`), the same way `Transcript.event_id` is cleared.
  A to-do whose occurrence was moved or canceled keeps its text and is shown
  under "Общие" if its meeting is no longer on the day.
- Workspace backup/restore must include the new table (follow how other
  tables are listed for backup).
- Database API: list for a day (applies the carry-over rule), add, set text,
  set done/undone, delete. Plus one lookup the "without notes" tile needs:
  which of a set of events / occurrences have a linked `ClientNote`.

## Architecture

- `src/pages/my_day_page/` (new library, like `transcript_page`):
  - `MyDayPage` — `QWidget` hosting a `QQuickWidget`, set up like
    `TranscriptListPage::enableQuickWorkspace` (OpenGL graphics API,
    `SizeRootObjectToView`, clear colour from the palette).
  - `MyDayBridge` — `QObject` exposed as a context property; exposes the
    selected day, tiles, timeline items (as `QVariantList` like
    `TranscriptListPage::records`) and to-dos; `Q_INVOKABLE` for day
    navigation, to-do edits and "open"/"join"; signals the page forwards
    to `MainWindow`/`Application`.
  - `qml/MyDay.qml` (+ small components) compiled in through
    `qt_add_resources` under a `/sessio/myday` prefix. Theme from
    `SystemPalette`, as `TranscriptLibrary.qml` does.
- Timeline data comes from `QTimelineModel::eventsForRange(day, day)` on the
  shared model (it returns copies and does not move the Calendar). The bridge
  rebuilds when the model signals changes (`eventsLoaded`, rows/data changed)
  and when the page is shown.
- Tile figures and to-dos come from new `Database` methods.
- `MainWindow`: add `Pages::myDay` (first in the enum list for clarity),
  `pageTitle` → `tr("My day")`, a `TabButton` first in the tab column with a
  new `:/icons/sun-solid-full.svg` (Font Awesome "sun", same set as the other
  icons) and label via the same translation-key style as the others
  (`tr(": NAV_MY_DAY")` → "Мой день" / "My day"). Startup shows `myDay`
  instead of `clientInfo`. Pages are keyed by enum in `QHash`es, not by
  position, but check tests and `showPage` callers for anything that assumes
  `clientInfo` is the start page. No "restore last page" setting exists.
- Strings: QML uses `qsTr` like the existing QML pages; add the new strings
  and the `NAV_MY_DAY` key to `translation/app_ru.ts` / `app_en.ts`.
  The sun icon is a new file to add under `resources/icons/`.
- `Application`: construct the page with the shared `QTimelineModel`, DB and
  `MeetingCoordinator`; wire jump/join signals the way the Calendar page's
  `openLiveKitMeetingRequested` is wired.

## Testing

- Database tests: DayTodo CRUD; carry-over rule for today/past/future;
  the linked-note lookup (event link and series + occurrence link).
- Bridge tests (no QML needed): tile values including virtual occurrences
  (status filters, 90-day and 7-day windows), grouping into morning/afternoon/
  evening, past/current/next flags against an injected clock, to-do
  grouping by client and by meeting.
- One QML smoke test that loads `MyDay.qml` and finds its root object, like
  the existing QML pages' tests.
- Manual: run via `scripts/run-dev-isolated.sh`.

## Left to the builder

- Exact sizes, spacing and colours (follow the existing QML pages and the
  Qlementine palette); tile wording beyond what is above.
- Whether the minute timer lives in C++ or QML.
- How the popup list for tiles is drawn (QML `Popup` is fine).
