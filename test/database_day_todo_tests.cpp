#include <Poco/File.h>
#include <Poco/Path.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <memory>
#include <string>

#include "config.h"
#include "database.h"

namespace {

constexpr int64_t kHour = 3600LL * 1000;

// Local midnight (epoch ms) of today shifted by `offsetDays` calendar days.
int64_t localMidnightMs(int offsetDays) {
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
  localtime_r(&now, &tm);
  tm.tm_hour = 0;
  tm.tm_min = 0;
  tm.tm_sec = 0;
  tm.tm_mday += offsetDays;
  tm.tm_isdst = -1;
  return static_cast<int64_t>(std::mktime(&tm)) * 1000;
}

class DayTodoDbTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
    dir_ = Poco::Path(Poco::Path::current())
               .append(std::string("tmp_day_todo_") + info->name())
               .toString();
    removeDir();
    pcm::config::Config conf{
        .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir_)}};
    db_ = std::make_unique<pcm::database::Database>(conf);
  }

  void TearDown() override {
    db_.reset();
    removeDir();
  }

  void removeDir() {
    Poco::File dir(dir_);
    if (dir.exists()) dir.remove(true);
  }

  int64_t addTodo(const std::string &title, int dayOffset,
                  std::optional<int64_t> doneAtMs = std::nullopt) {
    DuckDayTodo todo;
    todo.title = title;
    todo.day_ms = localMidnightMs(dayOffset);
    todo.done_at_ms = doneAtMs;
    return db_->add_day_todo(todo);
  }

  // Titles shown on the day at `offset` days from today.
  std::vector<std::string> titlesOn(int offset) {
    std::vector<std::string> titles;
    for (const auto &t : db_->get_day_todos(localMidnightMs(offset), localMidnightMs(offset + 1),
                                            offset == 0))
      titles.push_back(t.title);
    return titles;
  }

  static bool has(const std::vector<std::string> &v, const std::string &s) {
    return std::find(v.begin(), v.end(), s) != v.end();
  }

  std::string dir_;
  std::unique_ptr<pcm::database::Database> db_;
};

}  // namespace

TEST_F(DayTodoDbTest, addAndListOnSameDay) {
  DuckDayTodo todo;
  todo.title = "Call back";
  todo.day_ms = localMidnightMs(1);
  todo.client_id = 7;
  todo.event_id = 8;
  todo.series_id = 9;
  todo.occurrence_start_ms = 1730000000000;
  const auto id = db_->add_day_todo(todo);
  ASSERT_GT(id, 0);

  const auto rows = db_->get_day_todos(localMidnightMs(1), localMidnightMs(2), false);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].id, id);
  EXPECT_EQ(rows[0].title, "Call back");
  EXPECT_EQ(rows[0].day_ms, localMidnightMs(1));
  EXPECT_EQ(rows[0].client_id, 7);
  EXPECT_EQ(rows[0].event_id, 8);
  EXPECT_EQ(rows[0].series_id, 9);
  EXPECT_EQ(rows[0].occurrence_start_ms, 1730000000000);
  EXPECT_FALSE(rows[0].done_at_ms.has_value());
  EXPECT_GT(rows[0].created_at_ms, 0);
}

TEST_F(DayTodoDbTest, unfinishedCarriesOverToToday) {
  ASSERT_GT(addTodo("old", -2), 0);
  EXPECT_TRUE(has(titlesOn(0), "old"));
  EXPECT_FALSE(has(titlesOn(-1), "old"));
}

TEST_F(DayTodoDbTest, finishedCarryOverShowsOnlyOnFinishDay) {
  ASSERT_GT(addTodo("old", -2, localMidnightMs(0) + kHour), 0);
  EXPECT_TRUE(has(titlesOn(0), "old"));
  EXPECT_FALSE(has(titlesOn(-2), "old"));
}

TEST_F(DayTodoDbTest, pastDayShowsDatedUnfinishedAndFinishedThatDay) {
  ASSERT_GT(addTodo("dated-undone", -1), 0);
  ASSERT_GT(addTodo("done-that-day", -3, localMidnightMs(-1) + kHour), 0);
  ASSERT_GT(addTodo("done-today", -1, localMidnightMs(0) + kHour), 0);
  const auto titles = titlesOn(-1);
  EXPECT_TRUE(has(titles, "dated-undone"));
  EXPECT_TRUE(has(titles, "done-that-day"));
  EXPECT_FALSE(has(titles, "done-today"));
}

TEST_F(DayTodoDbTest, futureDayShowsOnlyDatedTodos) {
  ASSERT_GT(addTodo("tomorrow", 1), 0);
  ASSERT_GT(addTodo("old", -2), 0);
  const auto titles = titlesOn(1);
  EXPECT_TRUE(has(titles, "tomorrow"));
  EXPECT_FALSE(has(titles, "old"));
}

TEST_F(DayTodoDbTest, updateTitleAndToggleDone) {
  const auto id = addTodo("first", 0);
  ASSERT_GT(id, 0);
  ASSERT_TRUE(db_->update_day_todo_title(id, "renamed"));
  auto rows = db_->get_day_todos(localMidnightMs(0), localMidnightMs(1), true);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].title, "renamed");

  const auto doneAt = localMidnightMs(0) + kHour;
  ASSERT_TRUE(db_->set_day_todo_done(id, doneAt));
  rows = db_->get_day_todos(localMidnightMs(0), localMidnightMs(1), true);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].done_at_ms, doneAt);

  ASSERT_TRUE(db_->set_day_todo_done(id, std::nullopt));
  rows = db_->get_day_todos(localMidnightMs(0), localMidnightMs(1), true);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_FALSE(rows[0].done_at_ms.has_value());

  ASSERT_TRUE(db_->delete_day_todo(id));
  EXPECT_TRUE(db_->get_day_todos(localMidnightMs(0), localMidnightMs(1), true).empty());
}

TEST_F(DayTodoDbTest, deletingEventUnlinksTodo) {
  DuckEvent event;
  event.name = std::string{"Session"};
  event.start_date = 1730000000000;
  event.end_date = 1730003600000;
  event.duration = 3600;
  event.event_stat_id = 1;
  event.payment_stat_id = 1;
  const auto eventId = db_->add_event(event);
  ASSERT_GT(eventId, 0);

  DuckDayTodo todo;
  todo.title = "linked";
  todo.day_ms = localMidnightMs(0);
  todo.event_id = eventId;
  ASSERT_GT(db_->add_day_todo(todo), 0);

  ASSERT_TRUE(db_->remove_event(eventId));
  const auto rows = db_->get_day_todos(localMidnightMs(0), localMidnightMs(1), true);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_FALSE(rows[0].event_id.has_value());
}

TEST_F(DayTodoDbTest, deletingClientUnlinksTodo) {
  DuckClient client;
  client.name = std::string{"Anna"};
  const auto clientId = db_->add_client(client);
  ASSERT_GT(clientId, 0);

  DuckDayTodo todo;
  todo.title = "linked";
  todo.day_ms = localMidnightMs(0);
  todo.client_id = clientId;
  ASSERT_GT(db_->add_day_todo(todo), 0);

  ASSERT_TRUE(db_->remove_client(clientId));
  const auto rows = db_->get_day_todos(localMidnightMs(0), localMidnightMs(1), true);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_FALSE(rows[0].client_id.has_value());
}
