#include "main_window.h"
#include "my_day_bridge.h"
#include "my_day_page.h"
#include "qtimeline_model.h"

#include <Poco/Path.h>
#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>

namespace {

class MainWindowMyDayTest : public ::testing::Test {
protected:
  void SetUp() override {
    pcm::config::Config config{
        .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir.path().toStdString())}};
    db = std::make_shared<pcm::database::Database>(config);
    model = std::make_unique<QTimelineModel>(db, nullptr);
  }

  QTemporaryDir dir;
  std::shared_ptr<pcm::database::Database> db;
  std::unique_ptr<QTimelineModel> model;
};

TEST_F(MainWindowMyDayTest, StartsOnMyDay) {
  MainWindow window;
  window.addMyDayPage(db, model.get(), nullptr);

  auto *stack = window.findChild<QStackedWidget *>("stackedWidget");
  ASSERT_NE(stack, nullptr);
  EXPECT_NE(window.getPage(MainWindow::Pages::myDay), nullptr);
  EXPECT_EQ(stack->currentWidget(), window.getPage(MainWindow::Pages::myDay));
}

TEST_F(MainWindowMyDayTest, MyDayTabIsFirst) {
  MainWindow window;
  const auto tabs = window.findChildren<TabButton *>();
  ASSERT_FALSE(tabs.isEmpty());
  EXPECT_EQ(tabs.first()->accessibleName(), QCoreApplication::translate("MainWindow", ": NAV_MY_DAY"));
  EXPECT_TRUE(tabs.first()->isChecked());
}

TEST_F(MainWindowMyDayTest, OpenEventSwitchesToCalendar) {
  MainWindow window;
  window.addEventInfoPage(model.get(), nullptr);
  window.addMyDayPage(db, model.get(), nullptr);
  auto *stack = window.findChild<QStackedWidget *>("stackedWidget");
  ASSERT_NE(stack, nullptr);
  auto *myDay = dynamic_cast<pcm::my_day::MyDayPage *>(window.getPage(MainWindow::Pages::myDay));
  ASSERT_NE(myDay, nullptr);
  ASSERT_EQ(stack->currentWidget(), myDay);

  emit myDay->bridge()->openEventRequested(1, 1730000000000);

  EXPECT_EQ(stack->currentWidget(), window.getPage(MainWindow::Pages::eventInfo));
}

} // namespace

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("QT_QUICK_BACKEND", "software");
  QTemporaryDir home;
  qputenv("HOME", home.path().toUtf8());
  qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
