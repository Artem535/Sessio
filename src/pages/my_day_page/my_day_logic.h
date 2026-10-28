#pragma once

#include <QDate>
#include <QDateTime>
#include <QVector>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "database.h"
#include "schema.hpp"

namespace pcm::my_day {

enum class Section { Morning, Afternoon, Evening };
enum class TimeState { Past, Current, Next, Upcoming };

struct TimelineItem {
  DuckEvent event;
  Section section = Section::Morning;
  TimeState state = TimeState::Upcoming;
  bool canceled = false;
  bool unpaid = false;
  bool withoutNote = false;
  bool online = false;
};

struct Tiles {
  int meetings = 0;
  std::optional<QDateTime> nextStart;
  bool dayDone = false; // today only: no active event left to start or finish
  double plannedIncome = 0;
  int unpaidCount = 0;
  double unpaidSum = 0;
  int withoutNotesCount = 0;
};

using HasNoteFn = std::function<bool(const DuckEvent &)>;

// event_stat_id in (1 scheduled, 2 completed, 4 confirmed).
bool isActive(const DuckEvent &e);

// Events that started on an earlier day than `day` are placed in Morning.
Section sectionFor(const QDateTime &localStart, const QDate &day);

// Real id for stored events, series + original occurrence start for virtual ones.
NoteLinkKey noteKeyFor(const DuckEvent &e);

// `dayEvents`: projection of the shown day. Sorted by start, canceled included.
std::vector<TimelineItem> buildTimeline(const QVector<DuckEvent> &dayEvents, const QDate &day,
                                        const QDateTime &now, const HasNoteFn &hasNote);

// `recentEvents`: projection of [now - 90d, now].
Tiles buildTiles(const QVector<DuckEvent> &dayEvents, const QVector<DuckEvent> &recentEvents,
                 const QDate &day, const QDateTime &now, const HasNoteFn &hasNote);

// Active work events ended within 90 days with pending payment, by start.
std::vector<DuckEvent> unpaidEvents(const QVector<DuckEvent> &recent, const QDateTime &now);

// Active work events ended within 7 days without a linked note, by start.
std::vector<DuckEvent> eventsWithoutNotes(const QVector<DuckEvent> &recent, const QDateTime &now,
                                          const HasNoteFn &hasNote);

} // namespace pcm::my_day
