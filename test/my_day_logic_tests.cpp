#include <gtest/gtest.h>

#include "my_day_logic.h"

using namespace pcm::my_day;

namespace {

const QDate kDay(2026, 10, 28);
const QDateTime kNow(kDay, QTime(15, 5));

QDateTime at(const QDate &d, int h, int m = 0) { return QDateTime(d, QTime(h, m)); }

DuckEvent makeEvent(std::int64_t id, const QDateTime &start, const QDateTime &end,
                    bool work = true, std::int64_t stat = 1, std::int64_t pay = 1,
                    double cost = 0) {
  DuckEvent e;
  e.id = id;
  e.is_work_event = work;
  e.event_stat_id = stat;
  e.payment_stat_id = pay;
  e.start_date = start.toMSecsSinceEpoch();
  e.end_date = end.toMSecsSinceEpoch();
  e.cost = cost;
  return e;
}

DuckEvent today(std::int64_t id, int sh, int sm, int eh, int em, bool work = true,
                std::int64_t stat = 1, double cost = 0) {
  return makeEvent(id, at(kDay, sh, sm), at(kDay, eh, em), work, stat, 1, cost);
}

DuckEvent endedDaysAgo(std::int64_t id, int days, std::int64_t pay = 1) {
  const QDateTime end = kNow.addDays(-days);
  return makeEvent(id, end.addSecs(-3000), end, true, 2, pay, 100);
}

const HasNoteFn kNoNotes = [](const DuckEvent &) { return false; };

} // namespace

class MyDayLogicTest : public ::testing::Test {};

TEST_F(MyDayLogicTest, sectionsSplitAtNoonAndSix) {
  EXPECT_EQ(sectionFor(at(kDay, 11, 59), kDay), Section::Morning);
  EXPECT_EQ(sectionFor(at(kDay, 12, 0), kDay), Section::Afternoon);
  EXPECT_EQ(sectionFor(at(kDay, 17, 59), kDay), Section::Afternoon);
  EXPECT_EQ(sectionFor(at(kDay, 18, 0), kDay), Section::Evening);
}

TEST_F(MyDayLogicTest, pastCurrentNextStates) {
  QVector<DuckEvent> ev{today(1, 14, 0, 14, 50), today(2, 15, 0, 15, 50), today(3, 18, 0, 19, 0)};
  auto items = buildTimeline(ev, kDay, kNow, kNoNotes);
  ASSERT_EQ(items.size(), 3u);
  EXPECT_EQ(items[0].state, TimeState::Past);
  EXPECT_EQ(items[1].state, TimeState::Current);
  EXPECT_EQ(items[2].state, TimeState::Upcoming);

  QVector<DuckEvent> noCurrent{today(1, 14, 0, 14, 50), today(4, 15, 30, 16, 0),
                               today(3, 18, 0, 19, 0)};
  items = buildTimeline(noCurrent, kDay, kNow, kNoNotes);
  EXPECT_EQ(items[0].state, TimeState::Past);
  EXPECT_EQ(items[1].state, TimeState::Next);
  EXPECT_EQ(items[2].state, TimeState::Upcoming);
}

TEST_F(MyDayLogicTest, otherDaysHaveNoCurrentOrNext) {
  QVector<DuckEvent> ev{today(1, 15, 0, 15, 50), today(2, 18, 0, 19, 0)};
  for (const auto &i : buildTimeline(ev, kDay.addDays(-1), kNow, kNoNotes)) {
    EXPECT_EQ(i.state, TimeState::Past);
  }
  for (const auto &i : buildTimeline(ev, kDay.addDays(1), kNow, kNoNotes)) {
    EXPECT_EQ(i.state, TimeState::Upcoming);
  }
}

TEST_F(MyDayLogicTest, canceledIsDimmedAndNotCounted) {
  QVector<DuckEvent> ev{today(1, 16, 0, 17, 0, true, 3, 3000), today(2, 17, 0, 18, 0, true, 1, 500)};
  const auto items = buildTimeline(ev, kDay, kNow, kNoNotes);
  ASSERT_EQ(items.size(), 2u);
  EXPECT_TRUE(items[0].canceled);
  EXPECT_FALSE(items[1].canceled);
  EXPECT_EQ(items[1].state, TimeState::Next);
  const Tiles t = buildTiles(ev, {}, kDay, kNow, kNoNotes);
  EXPECT_EQ(t.meetings, 1);
  EXPECT_DOUBLE_EQ(t.plannedIncome, 500);
}

TEST_F(MyDayLogicTest, meetingsCountsPersonalIncomeOnlyWork) {
  QVector<DuckEvent> ev{today(1, 16, 0, 17, 0, true, 1, 5000), today(2, 17, 0, 18, 0, false, 1, 2000)};
  const Tiles t = buildTiles(ev, {}, kDay, kNow, kNoNotes);
  EXPECT_EQ(t.meetings, 2);
  EXPECT_DOUBLE_EQ(t.plannedIncome, 5000);
  ASSERT_TRUE(t.nextStart.has_value());
  EXPECT_EQ(*t.nextStart, at(kDay, 16, 0));
  EXPECT_FALSE(t.dayDone);
}

TEST_F(MyDayLogicTest, unpaidUses90DayWindowAndPending) {
  QVector<DuckEvent> recent{endedDaysAgo(1, 10), endedDaysAgo(2, 91), endedDaysAgo(3, 10, 2)};
  DuckEvent future = makeEvent(4, kNow.addSecs(600), kNow.addSecs(4200), true, 1, 1, 100);
  recent.push_back(future);
  const Tiles t = buildTiles({}, recent, kDay, kNow, [](const DuckEvent &) { return true; });
  EXPECT_EQ(t.unpaidCount, 1);
  EXPECT_DOUBLE_EQ(t.unpaidSum, 100);
  EXPECT_EQ(unpaidEvents(recent, kNow).size(), 1u);
}

TEST_F(MyDayLogicTest, withoutNotesUses7DayWindow) {
  QVector<DuckEvent> recent{endedDaysAgo(1, 6, 2), endedDaysAgo(2, 8, 2), endedDaysAgo(3, 3, 2)};
  const auto hasNote = [](const DuckEvent &e) { return e.id == 3; };
  const Tiles t = buildTiles({}, recent, kDay, kNow, hasNote);
  EXPECT_EQ(t.withoutNotesCount, 1);
  const auto list = eventsWithoutNotes(recent, kNow, hasNote);
  ASSERT_EQ(list.size(), 1u);
  EXPECT_EQ(list[0].id, 1);
}

TEST_F(MyDayLogicTest, virtualOccurrenceCountsAsUnpaidAndWithoutNote) {
  DuckEvent e = endedDaysAgo(-5, 2);
  e.is_virtual_occurrence = true;
  e.series_id = 7;
  e.original_occurrence_start = e.start_date;
  const Tiles t = buildTiles({}, {e}, kDay, kNow, kNoNotes);
  EXPECT_EQ(t.unpaidCount, 1);
  EXPECT_EQ(t.withoutNotesCount, 1);
  const NoteLinkKey key = noteKeyFor(e);
  EXPECT_FALSE(key.event_id.has_value());
  EXPECT_EQ(key.series_id, 7);
  EXPECT_EQ(key.occurrence_start_ms, e.start_date);
  EXPECT_EQ(noteKeyFor(endedDaysAgo(9, 1)).event_id, 9);
}

TEST_F(MyDayLogicTest, midnightCrossingEventShowsOnBothDays) {
  const QDate d(2026, 10, 27);
  QVector<DuckEvent> ev{makeEvent(1, at(d, 23, 30), at(d.addDays(1), 0, 30))};
  EXPECT_EQ(buildTimeline(ev, d, kNow, kNoNotes)[0].section, Section::Evening);
  EXPECT_EQ(buildTimeline(ev, d.addDays(1), kNow, kNoNotes)[0].section, Section::Morning);
}

TEST_F(MyDayLogicTest, dayDoneWhenNoUpcoming) {
  QVector<DuckEvent> ev{today(1, 9, 0, 10, 0), today(2, 13, 0, 14, 0)};
  const Tiles t = buildTiles(ev, {}, kDay, kNow, kNoNotes);
  EXPECT_TRUE(t.dayDone);
  EXPECT_FALSE(t.nextStart.has_value());
  EXPECT_EQ(t.meetings, 2);
}

TEST_F(MyDayLogicTest, onlineIncludesLiveKitProvider) {
  DuckEvent e = today(1, 16, 0, 17, 0);
  e.provider_kind = "LiveKit";
  EXPECT_TRUE(buildTimeline({e}, kDay, kNow, kNoNotes)[0].online);
  e.provider_kind = std::nullopt;
  EXPECT_FALSE(buildTimeline({e}, kDay, kNow, kNoNotes)[0].online);
  e.is_online = true;
  EXPECT_TRUE(buildTimeline({e}, kDay, kNow, kNoNotes)[0].online);
}

TEST_F(MyDayLogicTest, timelineCardTagsHaveNoAgeLimit) {
  DuckEvent old = endedDaysAgo(1, 100);
  old.event_stat_id = 1;
  const QDate oldDay = QDateTime::fromMSecsSinceEpoch(*old.start_date).date();
  auto item = buildTimeline({old}, oldDay, kNow, kNoNotes)[0];
  EXPECT_TRUE(item.unpaid);
  EXPECT_TRUE(item.withoutNote);

  const HasNoteFn hasNote = [](const DuckEvent &) { return true; };
  item = buildTimeline({old}, oldDay, kNow, hasNote)[0];
  EXPECT_FALSE(item.withoutNote);

  old.is_work_event = false;
  item = buildTimeline({old}, oldDay, kNow, kNoNotes)[0];
  EXPECT_FALSE(item.unpaid);
  EXPECT_FALSE(item.withoutNote);
}

namespace {

DuckDayTodo todo(std::int64_t id, std::optional<std::int64_t> client = std::nullopt,
                 std::optional<std::int64_t> event = std::nullopt) {
  DuckDayTodo t;
  t.id = id;
  t.title = "t" + std::to_string(id);
  t.client_id = client;
  t.event_id = event;
  return t;
}

const std::function<QString(int64_t)> kClientName = [](int64_t id) {
  switch (id) {
  case 10: return QStringLiteral("Борис");
  case 11: return QStringLiteral("Анна");
  default: return QString();
  }
};

// Event id -> client: 1 -> 10, 2 -> 11, others none.
const std::function<std::optional<int64_t>(const DuckEvent &)> kClientOf =
    [](const DuckEvent &e) -> std::optional<int64_t> {
  if (e.id == 1) return 10;
  if (e.id == 2) return 11;
  return std::nullopt;
};

} // namespace

TEST_F(MyDayLogicTest, byClientGroupsMeetingTodosUnderItsClient) {
  QVector<DuckEvent> ev{today(1, 10, 0, 10, 50)};
  std::vector<DuckDayTodo> todos{todo(1, 11), todo(2, std::nullopt, 1), todo(3, 10)};
  auto g = groupTodos(todos, ev, TodoGrouping::ByClient, kClientName, kClientOf);
  ASSERT_EQ(g.size(), 2u);
  EXPECT_EQ(g[0].title, "Анна");
  EXPECT_EQ(g[0].clientId, 11);
  ASSERT_EQ(g[0].todos.size(), 1u);
  EXPECT_EQ(g[0].todos[0].id, 1);
  EXPECT_EQ(g[1].title, "Борис");
  ASSERT_EQ(g[1].todos.size(), 2u);
  EXPECT_EQ(g[1].todos[0].id, 2);
  EXPECT_EQ(g[1].todos[1].id, 3);
}

TEST_F(MyDayLogicTest, byMeetingOrdersGroupsByStart) {
  QVector<DuckEvent> ev{today(2, 9, 0, 9, 50), today(1, 8, 30, 9, 20), today(3, 12, 0, 13, 0)};
  ev[2].name = "Supervision";
  std::vector<DuckDayTodo> todos{todo(1, std::nullopt, 3), todo(2, std::nullopt, 2),
                                 todo(3, std::nullopt, 1), todo(4, std::nullopt, 2)};
  auto g = groupTodos(todos, ev, TodoGrouping::ByMeeting, kClientName, kClientOf);
  ASSERT_EQ(g.size(), 3u);
  EXPECT_EQ(g[0].eventId, 1);
  EXPECT_EQ(g[0].title, "08:30 Борис");
  EXPECT_EQ(g[1].eventId, 2);
  EXPECT_EQ(g[1].title, "09:00 Анна");
  ASSERT_EQ(g[1].todos.size(), 2u);
  EXPECT_EQ(g[1].todos[0].id, 2);
  EXPECT_EQ(g[1].todos[1].id, 4);
  EXPECT_EQ(g[2].eventId, 3);
  EXPECT_EQ(g[2].title, "12:00 Supervision");
}

TEST_F(MyDayLogicTest, unlinkedGoesToGeneralLast) {
  QVector<DuckEvent> ev{today(1, 10, 0, 10, 50)};
  std::vector<DuckDayTodo> todos{todo(1), todo(2, 10)};
  for (auto mode : {TodoGrouping::ByClient, TodoGrouping::ByMeeting}) {
    auto g = groupTodos(todos, ev, mode, kClientName, kClientOf);
    ASSERT_FALSE(g.empty());
    EXPECT_EQ(g.back().title, QObject::tr("General"));
    EXPECT_FALSE(g.back().clientId);
    EXPECT_FALSE(g.back().eventId);
    EXPECT_EQ(g.back().todos.front().id, 1);
  }
}

TEST_F(MyDayLogicTest, todoWithMissingMeetingFallsToGeneral) {
  std::vector<DuckDayTodo> todos{todo(1, std::nullopt, 99)};
  auto g = groupTodos(todos, {}, TodoGrouping::ByMeeting, kClientName, kClientOf);
  ASSERT_EQ(g.size(), 1u);
  EXPECT_EQ(g[0].title, QObject::tr("General"));
}

TEST_F(MyDayLogicTest, todoWithDeletedClientFallsToGeneral) {
  std::vector<DuckDayTodo> todos{todo(1, 77)};
  auto g = groupTodos(todos, {}, TodoGrouping::ByClient, kClientName, kClientOf);
  ASSERT_EQ(g.size(), 1u);
  EXPECT_EQ(g[0].title, QObject::tr("General"));
  EXPECT_FALSE(g[0].clientId);
}

TEST_F(MyDayLogicTest, virtualOccurrenceTodoMatchesBySeriesAndOccurrence) {
  DuckEvent v = today(-5, 10, 0, 10, 50);
  v.series_id = 7;
  v.original_occurrence_start = 123456;
  v.is_virtual_occurrence = true;
  DuckDayTodo t = todo(1);
  t.series_id = 7;
  t.occurrence_start_ms = 123456;
  auto g = groupTodos({t}, {v}, TodoGrouping::ByMeeting, kClientName, kClientOf);
  ASSERT_EQ(g.size(), 1u);
  EXPECT_EQ(g[0].eventId, -5);
}
