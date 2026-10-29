#include "my_day_logic.h"

#include <QTimeZone>

#include <algorithm>
#include <limits>

#include "provider_kind.h"

namespace pcm::my_day {

namespace {

constexpr int kUnpaidWindowDays = 90;
constexpr int kNoteWindowDays = 7;
constexpr std::int64_t kPendingPaymentStat = 1;
constexpr std::int64_t kCanceledStat = 3;

QDateTime toLocal(const std::int64_t ms) {
  return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone(QTimeZone::LocalTime));
}

QDateTime startOf(const DuckEvent &e) { return toLocal(e.start_date.value_or(0)); }

QDateTime endOf(const DuckEvent &e) {
  return toLocal(e.end_date.value_or(e.start_date.value_or(0)));
}

bool isOnline(const DuckEvent &e) {
  if (e.is_online) {
    return true;
  }
  const auto kind =
      e.provider_kind ? meeting::providerKindFromString(*e.provider_kind) : std::nullopt;
  return meeting::isBackendHosted(kind);
}

bool endedBefore(const DuckEvent &e, const QDateTime &now) { return endOf(e) <= now; }

std::vector<DuckEvent> sortedByStart(std::vector<DuckEvent> events) {
  std::stable_sort(events.begin(), events.end(), [](const DuckEvent &a, const DuckEvent &b) {
    return a.start_date.value_or(0) < b.start_date.value_or(0);
  });
  return events;
}

} // namespace

bool isActive(const DuckEvent &e) {
  return e.event_stat_id == 1 || e.event_stat_id == 2 || e.event_stat_id == 4;
}

Section sectionFor(const QDateTime &localStart, const QDate &day) {
  if (localStart.date() < day) {
    return Section::Morning;
  }
  const int hour = localStart.time().hour();
  if (hour < 12) {
    return Section::Morning;
  }
  return hour < 18 ? Section::Afternoon : Section::Evening;
}

NoteLinkKey noteKeyFor(const DuckEvent &e) {
  NoteLinkKey key;
  if (e.is_virtual_occurrence && e.series_id && e.original_occurrence_start) {
    key.series_id = e.series_id;
    key.occurrence_start_ms = e.original_occurrence_start;
  } else {
    key.event_id = e.id;
  }
  return key;
}

std::vector<TimelineItem> buildTimeline(const QVector<DuckEvent> &dayEvents, const QDate &day,
                                        const QDateTime &now, const HasNoteFn &hasNote) {
  const std::vector<DuckEvent> sorted =
      sortedByStart(std::vector<DuckEvent>(dayEvents.begin(), dayEvents.end()));
  const bool isToday = day == now.date();
  const bool isPastDay = day < now.date();

  std::vector<TimelineItem> items;
  items.reserve(sorted.size());
  bool hasCurrent = false;
  for (const DuckEvent &e : sorted) {
    TimelineItem item;
    item.event = e;
    item.section = sectionFor(startOf(e), day);
    item.canceled = e.event_stat_id == kCanceledStat;
    item.online = isOnline(e);
    const bool active = isActive(e);
    const bool ended = endedBefore(e, now);
    if (isPastDay) {
      item.state = TimeState::Past;
    } else if (!isToday) {
      item.state = TimeState::Upcoming;
    } else if (ended) {
      item.state = TimeState::Past;
    } else if (active && startOf(e) <= now) {
      item.state = TimeState::Current;
      hasCurrent = true;
    }
    if (active && e.is_work_event && ended) {
      item.unpaid = e.payment_stat_id == kPendingPaymentStat;
      item.withoutNote = !hasNote(e);
    }
    items.push_back(std::move(item));
  }

  if (isToday && !hasCurrent) {
    for (TimelineItem &item : items) {
      if (isActive(item.event) && item.state == TimeState::Upcoming) {
        item.state = TimeState::Next;
        break;
      }
    }
  }
  return items;
}

std::vector<DuckEvent> unpaidEvents(const QVector<DuckEvent> &recent, const QDateTime &now) {
  const QDateTime since = now.addDays(-kUnpaidWindowDays);
  std::vector<DuckEvent> out;
  for (const DuckEvent &e : recent) {
    if (isActive(e) && e.is_work_event && e.payment_stat_id == kPendingPaymentStat &&
        endedBefore(e, now) && endOf(e) > since) {
      out.push_back(e);
    }
  }
  return sortedByStart(std::move(out));
}

std::vector<DuckEvent> eventsWithoutNotes(const QVector<DuckEvent> &recent, const QDateTime &now,
                                          const HasNoteFn &hasNote) {
  const QDateTime since = now.addDays(-kNoteWindowDays);
  std::vector<DuckEvent> out;
  for (const DuckEvent &e : recent) {
    if (isActive(e) && e.is_work_event && endedBefore(e, now) && endOf(e) > since &&
        !hasNote(e)) {
      out.push_back(e);
    }
  }
  return sortedByStart(std::move(out));
}

Tiles buildTiles(const QVector<DuckEvent> &dayEvents, const QVector<DuckEvent> &recentEvents,
                 const QDate &day, const QDateTime &now, const HasNoteFn &hasNote) {
  Tiles tiles;
  const bool isToday = day == now.date();
  bool anyLeft = false;
  for (const DuckEvent &e : dayEvents) {
    if (!isActive(e)) {
      continue;
    }
    ++tiles.meetings;
    if (e.is_work_event) {
      tiles.plannedIncome += e.cost.value_or(0);
    }
    const QDateTime start = startOf(e);
    if (!endedBefore(e, now)) {
      anyLeft = true;
    }
    const bool upcoming = !isToday || start > now;
    if (day >= now.date() && upcoming && (!tiles.nextStart || start < *tiles.nextStart)) {
      tiles.nextStart = start;
    }
  }
  tiles.dayDone = isToday && !anyLeft;

  for (const DuckEvent &e : unpaidEvents(recentEvents, now)) {
    ++tiles.unpaidCount;
    tiles.unpaidSum += e.cost.value_or(0);
  }
  tiles.withoutNotesCount = static_cast<int>(eventsWithoutNotes(recentEvents, now, hasNote).size());
  return tiles;
}

namespace {

const DuckEvent *findMeeting(const DuckDayTodo &t, const std::vector<DuckEvent> &events) {
  for (const DuckEvent &e : events) {
    if (t.event_id && *t.event_id == e.id) {
      return &e;
    }
    if (t.series_id && t.occurrence_start_ms && e.series_id == t.series_id &&
        e.original_occurrence_start == t.occurrence_start_ms) {
      return &e;
    }
  }
  return nullptr;
}

} // namespace

std::vector<TodoGroup>
groupTodos(const std::vector<DuckDayTodo> &todos, const QVector<DuckEvent> &dayEvents,
           const TodoGrouping mode, const std::function<QString(int64_t)> &clientName,
           const std::function<std::optional<int64_t>(const DuckEvent &)> &clientOf) {
  const std::vector<DuckEvent> events =
      sortedByStart(std::vector<DuckEvent>(dayEvents.begin(), dayEvents.end()));

  std::vector<TodoGroup> groups;
  TodoGroup general;
  general.title = QObject::tr("General");

  for (const DuckDayTodo &t : todos) {
    const DuckEvent *meeting = findMeeting(t, events);
    TodoGroup *target = &general;
    if (mode == TodoGrouping::ByMeeting) {
      if (meeting) {
        auto it = std::find_if(groups.begin(), groups.end(), [&](const TodoGroup &g) {
          return g.eventId == meeting->id;
        });
        if (it == groups.end()) {
          groups.emplace_back();
          it = groups.end() - 1;
          it->eventId = meeting->id;
          it->clientId = clientOf(*meeting);
        }
        target = &*it;
      }
    } else {
      std::optional<int64_t> client;
      QString name;
      if (t.client_id) {
        name = clientName(*t.client_id);
        if (!name.isEmpty()) {
          client = t.client_id;
        }
      }
      if (!client && meeting) {
        const auto viaMeeting = clientOf(*meeting);
        if (viaMeeting) {
          name = clientName(*viaMeeting);
          if (!name.isEmpty()) {
            client = viaMeeting;
          }
        }
      }
      if (client) {
        auto it = std::find_if(groups.begin(), groups.end(),
                               [&](const TodoGroup &g) { return g.clientId == client; });
        if (it == groups.end()) {
          groups.emplace_back();
          it = groups.end() - 1;
          it->clientId = client;
          it->title = name;
        }
        target = &*it;
      }
    }
    target->todos.push_back(t);
  }

  if (mode == TodoGrouping::ByClient) {
    std::stable_sort(groups.begin(), groups.end(), [](const TodoGroup &a, const TodoGroup &b) {
      return QString::localeAwareCompare(a.title, b.title) < 0;
    });
  } else {
    for (TodoGroup &g : groups) {
      const auto it = std::find_if(events.begin(), events.end(),
                                   [&](const DuckEvent &e) { return e.id == g.eventId; });
      if (it == events.end()) {
        continue;
      }
      QString label = it->name ? QString::fromStdString(*it->name) : QString();
      if (g.clientId) {
        const QString cn = clientName(*g.clientId);
        if (!cn.isEmpty()) {
          label = cn;
        }
      }
      g.title = startOf(*it).toString("HH:mm") + " " + label;
    }
    std::stable_sort(groups.begin(), groups.end(), [&](const TodoGroup &a, const TodoGroup &b) {
      auto start = [&](const TodoGroup &g) {
        const auto it = std::find_if(events.begin(), events.end(),
                                     [&](const DuckEvent &e) { return e.id == g.eventId; });
        return it == events.end() ? std::numeric_limits<int64_t>::max() : it->start_date.value_or(0);
      };
      return start(a) < start(b);
    });
  }

  if (!general.todos.empty()) {
    groups.push_back(std::move(general));
  }
  return groups;
}

} // namespace pcm::my_day
