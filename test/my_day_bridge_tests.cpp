#include <Poco/Path.h>
#include <gtest/gtest.h>

#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>
#include <memory>

#include "config.h"
#include "database.h"
#include "my_day_bridge.h"
#include "qtimeline_model.h"
#include "recurrence_utils.h"

namespace {

QDateTime at(const QDate &date, int hour, int minute = 0) {
  return QDateTime(date, QTime(hour, minute), QTimeZone(QTimeZone::LocalTime));
}

DuckEvent workEvent(const QDate &date, int hour, int minutes = 60) {
  DuckEvent e;
  e.name = "Session";
  e.is_work_event = true;
  e.start_date = at(date, hour).toMSecsSinceEpoch();
  e.end_date = *e.start_date + minutes * 60000LL;
  e.duration = minutes * 60;
  e.event_stat_id = 1;
  e.payment_stat_id = 1;
  e.cost = 100;
  return e;
}

class MyDayBridgeTest : public ::testing::Test {
protected:
  void SetUp() override {
    pcm::config::Config config{
        .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir.path().toStdString())}};
    db = std::make_shared<pcm::database::Database>(config);
    model = std::make_unique<QTimelineModel>(db, nullptr);
    today = QDate(2026, 10, 28);
    QSettings().remove("myDay/groupByMeeting");
  }

  std::unique_ptr<pcm::my_day::MyDayBridge> makeBridge() {
    const QDateTime fixed = at(today, 12);
    return std::make_unique<pcm::my_day::MyDayBridge>(db, model.get(), nullptr,
                                                      [fixed] { return fixed; });
  }

  QTemporaryDir dir;
  std::shared_ptr<pcm::database::Database> db;
  std::unique_ptr<QTimelineModel> model;
  QDate today;
};

TEST_F(MyDayBridgeTest, LoadingDayDoesNotMoveCalendarModel) {
  const QDate other = today.addDays(-5);
  model->loadEventsForDay(other);
  auto bridge = makeBridge();
  bridge->nextDay();
  EXPECT_EQ(model->currentDate(), other);
}

TEST_F(MyDayBridgeTest, TilesAndTimelineFromModel) {
  model->addEvent(workEvent(today, 9));
  model->addEvent(workEvent(today, 15));
  auto bridge = makeBridge();
  const QVariantMap tiles = bridge->tiles();
  EXPECT_EQ(tiles["meetings"].toInt(), 2);
  EXPECT_EQ(tiles["unpaidCount"].toInt(), 1);
  EXPECT_EQ(tiles["withoutNotesCount"].toInt(), 1);
  EXPECT_FALSE(tiles["meetingsSubtitle"].toString().isEmpty());
  const QVariantList timeline = bridge->timeline();
  ASSERT_EQ(timeline.size(), 2);
  EXPECT_EQ(timeline[0].toMap()["state"].toString(), "past");
  EXPECT_TRUE(timeline[0].toMap()["unpaid"].toBool());
  EXPECT_EQ(timeline[0].toMap()["section"].toString(), "morning");
  EXPECT_EQ(timeline[1].toMap()["state"].toString(), "next");
  EXPECT_EQ(timeline[1].toMap()["section"].toString(), "afternoon");
}

TEST_F(MyDayBridgeTest, FollowsTodayAcrossMidnight) {
  auto clock = std::make_shared<QDateTime>(at(today, 23, 59));
  pcm::my_day::MyDayBridge bridge(db, model.get(), nullptr, [clock] { return *clock; });
  ASSERT_TRUE(bridge.isToday());
  const QString before = bridge.dateTitle();
  *clock = at(today.addDays(1), 0, 1);
  bridge.reload();
  EXPECT_TRUE(bridge.isToday());
  EXPECT_NE(bridge.dateTitle(), before);
}

TEST_F(MyDayBridgeTest, StaysOnBrowsedDayAcrossMidnight) {
  auto clock = std::make_shared<QDateTime>(at(today, 23, 59));
  pcm::my_day::MyDayBridge bridge(db, model.get(), nullptr, [clock] { return *clock; });
  bridge.nextDay();
  ASSERT_FALSE(bridge.isToday());
  const QString before = bridge.dateTitle();
  *clock = at(today.addDays(1), 0, 1);
  bridge.reload();
  EXPECT_EQ(bridge.dateTitle(), before);
  EXPECT_TRUE(bridge.isToday()); // the browsed day (tomorrow) is now today
}

TEST_F(MyDayBridgeTest, EmptyDayHasNoSubtitle) {
  auto bridge = makeBridge();
  EXPECT_EQ(bridge->tiles()["meetings"].toInt(), 0);
  EXPECT_TRUE(bridge->tiles()["meetingsSubtitle"].toString().isEmpty());
}

TEST_F(MyDayBridgeTest, OpenEventPassesVirtualIdToCalendar) {
  DuckEvent base = workEvent(today, 15);
  const int64_t seriesId = model->addEventSeries(base, 0, pcm::recurrence::weeklyRuleForDate(today),
                                                 std::nullopt);
  ASSERT_GT(seriesId, 0);
  auto bridge = makeBridge();
  const QVariantList timeline = bridge->timeline();
  ASSERT_EQ(timeline.size(), 1);
  const qint64 id = timeline[0].toMap()["id"].toLongLong();
  const qint64 dayMs = timeline[0].toMap()["dayMs"].toLongLong();
  ASSERT_LT(id, 0);
  QSignalSpy spy(bridge.get(), &pcm::my_day::MyDayBridge::openEventRequested);
  bridge->openEvent(id, dayMs);
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.first().at(0).toLongLong(), id);
  EXPECT_EQ(spy.first().at(1).toLongLong(), dayMs);
  EXPECT_TRUE(pcm::recurrence::virtualOccurrenceForId(*db, id).has_value());
}

TEST_F(MyDayBridgeTest, AddTodoWithVirtualMeetingStoresSeriesLink) {
  const int64_t seriesId = model->addEventSeries(
      workEvent(today, 15), 0, pcm::recurrence::weeklyRuleForDate(today), std::nullopt);
  ASSERT_GT(seriesId, 0);
  auto bridge = makeBridge();
  const qint64 virtualId = bridge->timeline().at(0).toMap()["id"].toLongLong();
  bridge->addTodo("x", 0, virtualId);
  const auto dayStart = at(today, 0).toMSecsSinceEpoch();
  const auto todos = db->get_day_todos(dayStart, dayStart + 86400000LL, true);
  ASSERT_EQ(todos.size(), 1u);
  EXPECT_EQ(todos[0].series_id, seriesId);
  EXPECT_EQ(todos[0].occurrence_start_ms, at(today, 15).toMSecsSinceEpoch());
  EXPECT_FALSE(todos[0].event_id.has_value());
}

TEST_F(MyDayBridgeTest, AddTodoWithRealMeetingStoresEventId) {
  const int64_t id = model->addEvent(workEvent(today, 15));
  auto bridge = makeBridge();
  bridge->addTodo("  call back  ", 0, id);
  bridge->addTodo("   ", 0, 0);
  const auto dayStart = at(today, 0).toMSecsSinceEpoch();
  const auto todos = db->get_day_todos(dayStart, dayStart + 86400000LL, true);
  ASSERT_EQ(todos.size(), 1u);
  EXPECT_EQ(todos[0].title, "call back");
  EXPECT_EQ(todos[0].event_id, id);
  EXPECT_EQ(bridge->todoTotal(), 1);
  EXPECT_EQ(bridge->todoDone(), 0);
}

TEST_F(MyDayBridgeTest, SetTodoDoneStoresNow) {
  auto bridge = makeBridge();
  bridge->addTodo("a", 0, 0);
  const auto dayStart = at(today, 0).toMSecsSinceEpoch();
  const int64_t id = db->get_day_todos(dayStart, dayStart + 86400000LL, true).at(0).id;
  bridge->setTodoDone(id, true);
  EXPECT_EQ(bridge->todoDone(), 1);
  EXPECT_EQ(db->get_day_todos(dayStart, dayStart + 86400000LL, true).at(0).done_at_ms,
            at(today, 12).toMSecsSinceEpoch());
  bridge->setTodoDone(id, false);
  EXPECT_EQ(bridge->todoDone(), 0);
  bridge->removeTodo(id);
  EXPECT_EQ(bridge->todoTotal(), 0);
}

TEST_F(MyDayBridgeTest, DayNavigationAndToday) {
  auto bridge = makeBridge();
  EXPECT_TRUE(bridge->isToday());
  bridge->nextDay();
  EXPECT_FALSE(bridge->isToday());
  bridge->goToday();
  EXPECT_TRUE(bridge->isToday());
  bridge->previousDay();
  EXPECT_FALSE(bridge->isToday());
}

TEST_F(MyDayBridgeTest, ModelChangeTriggersReload) {
  auto bridge = makeBridge();
  QSignalSpy spy(bridge.get(), &pcm::my_day::MyDayBridge::changed);
  model->addEvent(workEvent(today, 15));
  QCoreApplication::processEvents(); // reloads from model signals are coalesced onto the loop
  EXPECT_GE(spy.count(), 1);
  EXPECT_EQ(bridge->tiles()["meetings"].toInt(), 1);
}

TEST_F(MyDayBridgeTest, GroupByMeetingPersists) {
  auto bridge = makeBridge();
  EXPECT_FALSE(bridge->groupByMeeting());
  bridge->setGroupByMeeting(true);
  EXPECT_TRUE(makeBridge()->groupByMeeting());
}

TEST_F(MyDayBridgeTest, HiddenBridgeDefersReload) {
  auto bridge = makeBridge();
  bridge->setActive(false);
  QSignalSpy changed(bridge.get(), &pcm::my_day::MyDayBridge::changed);
  model->addEvent(workEvent(today, 14));
  QCoreApplication::processEvents();
  EXPECT_EQ(changed.count(), 0);
  bridge->setActive(true);
  QCoreApplication::processEvents();
  EXPECT_EQ(changed.count(), 1);
  EXPECT_EQ(bridge->timeline().size(), 1);
}

TEST_F(MyDayBridgeTest, ModelBurstCoalescesIntoOneReload) {
  auto bridge = makeBridge();
  QSignalSpy changed(bridge.get(), &pcm::my_day::MyDayBridge::changed);
  model->addEvent(workEvent(today, 9));
  model->addEvent(workEvent(today, 10));
  QCoreApplication::processEvents();
  EXPECT_EQ(changed.count(), 1);
  EXPECT_EQ(bridge->timeline().size(), 2);
}

} // namespace

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QTemporaryDir settingsDir;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
  QApplication::setOrganizationName("SessioTest");
  QApplication::setApplicationName("MyDayBridgeTest");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
