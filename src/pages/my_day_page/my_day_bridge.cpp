#include "my_day_bridge.h"

#include <QLocale>
#include <QSettings>
#include <QTimeZone>

#include <cmath>

#include "app_settings.h"
#include "meeting_join.h"
#include "meeting_utils.h"
#include "qtimeline_model.h"
#include "recurrence_utils.h"
#include "series_call_service.h"

namespace pcm::my_day {

namespace {

constexpr auto kGroupByMeetingKey = "myDay/groupByMeeting";
constexpr int kRecentWindowDays = 90;

QDateTime toLocal(const std::int64_t ms) {
  return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone(QTimeZone::LocalTime));
}

qint64 midnightMs(const QDate &day) {
  return QDateTime(day, QTime(0, 0), QTimeZone(QTimeZone::LocalTime)).toMSecsSinceEpoch();
}

QString noteCacheKey(const NoteLinkKey &key) {
  if (key.event_id) {
    return QStringLiteral("e%1").arg(*key.event_id);
  }
  return QStringLiteral("s%1@%2").arg(key.series_id.value_or(0)).arg(key.occurrence_start_ms.value_or(0));
}

QString sectionName(const Section section) {
  switch (section) {
  case Section::Morning:
    return QStringLiteral("morning");
  case Section::Afternoon:
    return QStringLiteral("afternoon");
  case Section::Evening:
    return QStringLiteral("evening");
  }
  return QStringLiteral("morning");
}

QString stateName(const TimeState state) {
  switch (state) {
  case TimeState::Past:
    return QStringLiteral("past");
  case TimeState::Current:
    return QStringLiteral("current");
  case TimeState::Next:
    return QStringLiteral("next");
  case TimeState::Upcoming:
    return QStringLiteral("upcoming");
  }
  return QStringLiteral("upcoming");
}

QString timeRange(const DuckEvent &e) {
  const auto start = toLocal(e.start_date.value_or(0));
  const auto end = toLocal(e.end_date.value_or(e.start_date.value_or(0)));
  return start.toString("HH:mm") + QStringLiteral("–") + end.toString("HH:mm");
}

} // namespace

MyDayBridge::MyDayBridge(std::shared_ptr<pcm::database::Database> db, QTimelineModel *model,
                         pcm::meeting::SeriesCallService *seriesCalls, Clock clock,
                         QObject *parent)
    : QObject(parent), mDb(std::move(db)), mModel(model), mSeriesCalls(seriesCalls),
      mClock(std::move(clock)) {
  const bool realClock = !mClock;
  if (realClock) {
    mClock = [] { return QDateTime::currentDateTime(); };
  }
  mDay = now().date();
  mLastToday = mDay;
  mGroupByMeeting = QSettings().value(kGroupByMeetingKey, false).toBool();

  if (mModel) {
    connect(mModel, &QTimelineModel::eventsLoaded, this, &MyDayBridge::scheduleReload);
    connect(mModel, &QAbstractItemModel::dataChanged, this, &MyDayBridge::scheduleReload);
    connect(mModel, &QAbstractItemModel::rowsInserted, this, &MyDayBridge::scheduleReload);
    connect(mModel, &QAbstractItemModel::rowsRemoved, this, &MyDayBridge::scheduleReload);
    connect(mModel, &QAbstractItemModel::modelReset, this, &MyDayBridge::scheduleReload);
  }

  // Refreshes "next in N min", dimming and the highlight. An injected (fixed) clock never
  // advances, so the timer only runs with the real clock.
  mMinuteTimer.setInterval(60 * 1000);
  connect(&mMinuteTimer, &QTimer::timeout, this, &MyDayBridge::scheduleReload);
  if (realClock) {
    mMinuteTimer.start();
  }

  reload();
}

QDateTime MyDayBridge::now() const { return mClock(); }

// Model signals arrive in bursts (one per added row, per data change...): collapse them into one
// reload on the next event-loop turn, and skip the work entirely while the page is hidden.
void MyDayBridge::scheduleReload() {
  if (!mActive) {
    mDirty = true;
    return;
  }
  if (mReloadPending) {
    return;
  }
  mReloadPending = true;
  QTimer::singleShot(0, this, [this] {
    mReloadPending = false;
    if (!mActive) {
      mDirty = true;
      return;
    }
    reload();
  });
}

bool MyDayBridge::setActive(const bool active) {
  if (mActive == active) {
    return false;
  }
  mActive = active;
  if (mActive && mDirty) {
    reload();
    return true;
  }
  return false;
}

void MyDayBridge::setDay(const QDate &day) {
  mDay = day;
  reload();
}

void MyDayBridge::previousDay() { setDay(mDay.addDays(-1)); }
void MyDayBridge::nextDay() { setDay(mDay.addDays(1)); }
void MyDayBridge::goToday() { setDay(now().date()); }

void MyDayBridge::setGroupByMeeting(const bool value) {
  if (mGroupByMeeting == value) {
    return;
  }
  mGroupByMeeting = value;
  QSettings().setValue(kGroupByMeetingKey, value);
  reload();
}

std::optional<int64_t> MyDayBridge::clientIdFor(const DuckEvent &event) {
  if (event.is_virtual_occurrence || event.id <= 0) {
    if (event.series_id && mModel) {
      const auto series = mModel->eventSeriesById(*event.series_id);
      if (series) {
        return series->client_id;
      }
    }
    return std::nullopt;
  }
  if (const auto it = mEventClientCache.constFind(event.id); it != mEventClientCache.constEnd()) {
    return it.value();
  }
  std::optional<int64_t> result;
  try {
    const DuckClient client = mDb->get_client_by_event(event.id);
    if (client.id > 0) {
      result = client.id;
    }
  } catch (const std::exception &) {
    // Event without a client.
  }
  mEventClientCache.insert(event.id, result);
  return result;
}

QString MyDayBridge::clientNameFor(const int64_t clientId) const {
  return mClientNames.value(clientId);
}

QString MyDayBridge::titleFor(const DuckEvent &event) const {
  if (event.client_name && !event.client_name->empty()) {
    return QString::fromStdString(*event.client_name);
  }
  return event.name ? QString::fromStdString(*event.name) : QString();
}

QVariantList MyDayBridge::eventRows(const std::vector<DuckEvent> &events) {
  QVariantList rows;
  for (const DuckEvent &e : events) {
    const QDateTime start = toLocal(e.start_date.value_or(0));
    QVariantMap row;
    row["id"] = static_cast<qint64>(e.id);
    row["dayMs"] = midnightMs(start.date());
    row["date"] = start.date().toString("dd.MM");
    row["time"] = start.toString("HH:mm");
    row["title"] = titleFor(e);
    row["sum"] = e.cost.value_or(0);
    rows.append(row);
  }
  return rows;
}

void MyDayBridge::reload() {
  try {
    loadState();
  } catch (const std::exception &e) {
    PLOG_ERROR << "My day reload failed: " << e.what();
  }
}

void MyDayBridge::loadState() {
  mDirty = false;
  const QDateTime current = now();
  // Left open past midnight while showing "today": follow the calendar. A browsed day stays.
  if (current.date() != mLastToday) {
    if (mDay == mLastToday) {
      mDay = current.date();
    }
    mLastToday = current.date();
  }
  mNoteCache.clear();
  mEventClientCache.clear();
  mClientNames.clear();
  for (const auto &client : mDb->get_clients()) {
    mClientNames.insert(client->id, pcm::recurrence::fullClientName(*client));
  }

  mDayEvents.clear();
  mRecentEvents.clear();
  if (mModel) {
    mDayEvents = mModel->eventsForRange(mDay, mDay);
    mRecentEvents =
        mModel->eventsForRange(current.date().addDays(-kRecentWindowDays), current.date());
  }

  // One batched lookup for every event that can need a note answer.
  std::vector<NoteLinkKey> keys;
  QStringList keyNames;
  auto collect = [&](const QVector<DuckEvent> &events) {
    for (const DuckEvent &e : events) {
      const NoteLinkKey key = noteKeyFor(e);
      const QString name = noteCacheKey(key);
      if (!mNoteCache.contains(name)) {
        mNoteCache.insert(name, false);
        keys.push_back(key);
        keyNames.append(name);
      }
    }
  };
  collect(mDayEvents);
  collect(mRecentEvents);
  if (!keys.empty()) {
    const std::vector<bool> found = mDb->has_linked_notes(keys);
    for (size_t i = 0; i < keys.size() && i < found.size(); ++i) {
      mNoteCache[keyNames[static_cast<qsizetype>(i)]] = found[i];
    }
  }
  const HasNoteFn hasNote = [this](const DuckEvent &e) {
    return mNoteCache.value(noteCacheKey(noteKeyFor(e)), false);
  };

  mIsToday = mDay == current.date();
  mDateTitle = QLocale().toString(mDay, QStringLiteral("dddd, d MMMM"));

  // Tiles.
  const Tiles tiles = buildTiles(mDayEvents, mRecentEvents, mDay, current, hasNote);
  QString subtitle;
  if (tiles.meetings > 0) {
    if (mIsToday) {
      if (tiles.nextStart) {
        const int minutes =
            std::max(1, static_cast<int>(std::ceil(current.secsTo(*tiles.nextStart) / 60.0)));
        subtitle = minutes < 60 ? tr("next in %n min", nullptr, minutes)
                                : tr("next at %1").arg(tiles.nextStart->toString("HH:mm"));
      } else if (tiles.dayDone) {
        subtitle = tr("All done for today");
      }
    } else if (mDay > current.date() && tiles.nextStart) {
      subtitle = tr("first at %1").arg(tiles.nextStart->toString("HH:mm"));
    }
  }
  mTiles.clear();
  mTiles["meetings"] = tiles.meetings;
  mTiles["meetingsSubtitle"] = subtitle;
  mTiles["income"] = tiles.plannedIncome;
  mTiles["currency"] = pcm::app_settings::currencySymbol();
  mTiles["unpaidCount"] = tiles.unpaidCount;
  mTiles["unpaidSum"] = tiles.unpaidSum;
  mTiles["withoutNotesCount"] = tiles.withoutNotesCount;

  // Timeline.
  mTimeline.clear();
  const qint64 dayMs = midnightMs(mDay);
  for (const TimelineItem &item : buildTimeline(mDayEvents, mDay, current, hasNote)) {
    const DuckEvent &e = item.event;
    QVariantMap row;
    row["id"] = static_cast<qint64>(e.id);
    row["dayMs"] = dayMs;
    row["title"] = titleFor(e);
    row["start"] = toLocal(e.start_date.value_or(0)).toString("HH:mm");
    row["timeRange"] = timeRange(e);
    row["section"] = sectionName(item.section);
    row["state"] = stateName(item.state);
    row["canceled"] = item.canceled;
    row["unpaid"] = item.unpaid;
    row["withoutNote"] = item.withoutNote;
    row["online"] = item.online;
    row["personal"] = !e.is_work_event;
    row["inPerson"] = e.is_work_event && !item.online;
    const auto seriesTarget =
        mSeriesCalls ? mSeriesCalls->joinTargetFor(e) : std::optional<QString>{};
    row["joinable"] = pcm::meeting::resolveJoinAction(e, seriesTarget).kind !=
                      pcm::meeting::JoinAction::Kind::None;
    mTimeline.append(row);
  }

  // To-dos.
  const qint64 dayEndMs = midnightMs(mDay.addDays(1));
  const std::vector<DuckDayTodo> todos = mDb->get_day_todos(dayMs, dayEndMs, mIsToday);
  mTodoTotal = static_cast<int>(todos.size());
  mTodoDone = 0;
  for (const DuckDayTodo &t : todos) {
    if (t.done_at_ms) {
      ++mTodoDone;
    }
  }
  const auto groups = groupTodos(
      todos, mDayEvents, mGroupByMeeting ? TodoGrouping::ByMeeting : TodoGrouping::ByClient,
      [this](const int64_t id) { return clientNameFor(id); },
      [this](const DuckEvent &e) { return clientIdFor(e); });
  mTodoGroups.clear();
  for (const TodoGroup &g : groups) {
    QVariantMap group;
    group["title"] = g.title;
    group["clientId"] = g.clientId ? QVariant::fromValue<qint64>(*g.clientId) : QVariant();
    group["eventId"] = g.eventId ? QVariant::fromValue<qint64>(*g.eventId) : QVariant();
    group["dayMs"] = dayMs;
    QVariantList rows;
    for (const DuckDayTodo &t : g.todos) {
      QVariantMap row;
      row["id"] = static_cast<qint64>(t.id);
      row["title"] = QString::fromStdString(t.title);
      row["done"] = t.done_at_ms.has_value();
      row["clientId"] = t.client_id ? QVariant::fromValue<qint64>(*t.client_id) : QVariant();
      row["eventId"] = t.event_id ? QVariant::fromValue<qint64>(*t.event_id) : QVariant();
      row["carriedFrom"] = (mIsToday && t.day_ms < dayMs)
                               ? QLocale().toString(toLocal(t.day_ms).date(), QStringLiteral("d MMM"))
                               : QString();
      rows.append(row);
    }
    group["todos"] = rows;
    mTodoGroups.append(group);
  }

  emit changed();
}

QVariantList MyDayBridge::unpaidList() {
  try {
    return eventRows(unpaidEvents(mRecentEvents, now()));
  } catch (const std::exception &e) {
    PLOG_ERROR << "My day unpaid list failed: " << e.what();
    return {};
  }
}

QVariantList MyDayBridge::withoutNotesList() {
  try {
    return eventRows(eventsWithoutNotes(mRecentEvents, now(), [this](const DuckEvent &e) {
      return mNoteCache.value(noteCacheKey(noteKeyFor(e)), false);
    }));
  } catch (const std::exception &e) {
    PLOG_ERROR << "My day notes list failed: " << e.what();
    return {};
  }
}

QVariantList MyDayBridge::clientChoices() {
  QVariantList list;
  try {
    for (const auto &client : mDb->get_clients()) {
      if (!client->client_active) {
        continue;
      }
      QVariantMap row;
      row["id"] = static_cast<qint64>(client->id);
      row["name"] = pcm::recurrence::fullClientName(*client);
      list.append(row);
    }
  } catch (const std::exception &e) {
    PLOG_ERROR << "My day client choices failed: " << e.what();
  }
  return list;
}

QVariantList MyDayBridge::meetingChoices() {
  QVariantList list;
  try {
    for (const TimelineItem &item : buildTimeline(mDayEvents, mDay, now(), [](const DuckEvent &) {
           return true;
         })) {
      if (item.canceled) {
        continue;
      }
      QVariantMap row;
      row["id"] = static_cast<qint64>(item.event.id);
      row["title"] = titleFor(item.event);
      row["time"] = toLocal(item.event.start_date.value_or(0)).toString("HH:mm");
      const auto clientId = clientIdFor(item.event);
      row["clientId"] = clientId ? static_cast<qint64>(*clientId) : qint64{0};
      list.append(row);
    }
  } catch (const std::exception &e) {
    PLOG_ERROR << "My day meeting choices failed: " << e.what();
  }
  return list;
}

const DuckEvent *MyDayBridge::findDayEvent(const int64_t id) const {
  for (const DuckEvent &e : mDayEvents) {
    if (e.id == id) {
      return &e;
    }
  }
  return nullptr;
}

std::optional<DuckEvent> MyDayBridge::resolveEvent(const int64_t id) {
  if (const DuckEvent *e = findDayEvent(id)) {
    return *e;
  }
  for (const DuckEvent &e : mRecentEvents) {
    if (e.id == id) {
      return e;
    }
  }
  if (id < 0) {
    return pcm::recurrence::virtualOccurrenceForId(*mDb, id);
  }
  return std::nullopt;
}

void MyDayBridge::addTodo(const QString &title, const qint64 clientId, const qint64 eventId) {
  const QString trimmed = title.trimmed();
  if (trimmed.isEmpty()) {
    return;
  }
  try {
    DuckDayTodo todo;
    todo.title = trimmed.toStdString();
    todo.day_ms = midnightMs(mDay);
    if (clientId > 0) {
      todo.client_id = clientId;
    }
    if (eventId != 0) {
      const auto event = resolveEvent(eventId);
      if (event) {
        if (eventId < 0) {
          if (event->series_id && event->original_occurrence_start) {
            todo.series_id = event->series_id;
            todo.occurrence_start_ms = event->original_occurrence_start;
          }
        } else {
          todo.event_id = eventId;
        }
        if (!todo.client_id && (todo.event_id || todo.series_id)) {
          todo.client_id = clientIdFor(*event);
        }
      } else {
        PLOG_WARNING << "My day: meeting " << eventId << " not found, saving to-do unlinked";
      }
    }
    if (mDb->add_day_todo(todo) <= 0) {
      PLOG_WARNING << "My day: failed to add to-do";
    }
  } catch (const std::exception &e) {
    PLOG_ERROR << "My day addTodo failed: " << e.what();
  }
  reload();
}

void MyDayBridge::setTodoTitle(const qint64 id, const QString &title) {
  const QString trimmed = title.trimmed();
  if (trimmed.isEmpty()) {
    return;
  }
  try {
    if (!mDb->update_day_todo_title(id, trimmed.toStdString())) {
      PLOG_WARNING << "My day: failed to rename to-do " << id;
    }
  } catch (const std::exception &e) {
    PLOG_ERROR << "My day setTodoTitle failed: " << e.what();
  }
  reload();
}

void MyDayBridge::setTodoDone(const qint64 id, const bool done) {
  try {
    if (!mDb->set_day_todo_done(id, done ? std::optional<int64_t>(now().toMSecsSinceEpoch())
                                         : std::nullopt)) {
      PLOG_WARNING << "My day: failed to update to-do " << id;
    }
  } catch (const std::exception &e) {
    PLOG_ERROR << "My day setTodoDone failed: " << e.what();
  }
  reload();
}

void MyDayBridge::removeTodo(const qint64 id) {
  try {
    if (!mDb->delete_day_todo(id)) {
      PLOG_WARNING << "My day: failed to delete to-do " << id;
    }
  } catch (const std::exception &e) {
    PLOG_ERROR << "My day removeTodo failed: " << e.what();
  }
  reload();
}

void MyDayBridge::openEvent(const qint64 eventId, const qint64 dayMs) {
  emit openEventRequested(eventId, dayMs);
}

void MyDayBridge::joinEvent(const qint64 eventId) {
  pcm::meeting::JoinAction action;
  try {
    const auto event = resolveEvent(eventId);
    if (!event) {
      return;
    }
    const auto seriesTarget =
        mSeriesCalls ? mSeriesCalls->joinTargetFor(*event) : std::optional<QString>{};
    action = pcm::meeting::resolveJoinAction(*event, seriesTarget);
  } catch (const std::exception &e) {
    PLOG_ERROR << "My day joinEvent failed: " << e.what();
    return;
  }
  switch (action.kind) {
  case pcm::meeting::JoinAction::Kind::LiveKit:
    emit openLiveKitMeetingRequested(action.target);
    break;
  case pcm::meeting::JoinAction::Kind::Url:
    pcm::meeting::openMeetingUrl(action.target);
    break;
  case pcm::meeting::JoinAction::Kind::None:
    break;
  }
}

void MyDayBridge::openClient(const qint64 clientId) { emit openClientRequested(clientId); }

} // namespace pcm::my_day
