#include <Poco/Path.h>
#include <gtest/gtest.h>

#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QQuickItem>
#include <QQuickWidget>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <memory>

#include "config.h"
#include "database.h"
#include "my_day_bridge.h"
#include "my_day_page.h"
#include "qtimeline_model.h"

namespace {

QStringList gWarnings;
QtMessageHandler gPreviousHandler = nullptr;

// QML load/binding problems are reported as warnings: collect them so tests can fail on any.
void captureHandler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
  if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
    gWarnings.append(message);
  }
  if (gPreviousHandler) {
    gPreviousHandler(type, context, message);
  }
}

DuckEvent workEvent(const QDate &date, int hour, int minutes = 60) {
  DuckEvent e;
  e.name = "Session";
  e.is_work_event = true;
  e.start_date =
      QDateTime(date, QTime(hour, 0), QTimeZone(QTimeZone::LocalTime)).toMSecsSinceEpoch();
  e.end_date = *e.start_date + minutes * 60000LL;
  e.duration = minutes * 60;
  e.event_stat_id = 1;
  e.payment_stat_id = 1;
  e.cost = 100;
  return e;
}

class MyDayPageTest : public ::testing::Test {
protected:
  void SetUp() override {
    gWarnings.clear();
    pcm::config::Config config{
        .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir.path().toStdString())}};
    db = std::make_shared<pcm::database::Database>(config);
    model = std::make_unique<QTimelineModel>(db, nullptr);
    QSettings().remove("myDay/groupByMeeting");
  }

  QQuickWidget *quickOf(pcm::my_day::MyDayPage &page) {
    auto *quick = page.findChild<QQuickWidget *>("myDayQuickWidget");
    EXPECT_NE(quick, nullptr);
    return quick;
  }

  QTemporaryDir dir;
  std::shared_ptr<pcm::database::Database> db;
  std::unique_ptr<QTimelineModel> model;
};

TEST_F(MyDayPageTest, QmlLoadsAndFindsRoot) {
  pcm::my_day::MyDayPage page(db, model.get(), nullptr);
  page.resize(1100, 700);
  page.show();
  auto *quick = quickOf(page);
  ASSERT_NE(quick, nullptr);
  ASSERT_EQ(quick->status(), QQuickWidget::Ready);
  ASSERT_NE(quick->rootObject(), nullptr);
  EXPECT_EQ(quick->rootObject()->objectName(), "qmlMyDay");
  EXPECT_TRUE(gWarnings.isEmpty()) << gWarnings.join("\n").toStdString();
}

TEST_F(MyDayPageTest, HeaderShowsBridgeDateTitle) {
  pcm::my_day::MyDayPage page(db, model.get(), nullptr);
  page.resize(1100, 700);
  page.show();
  auto *quick = quickOf(page);
  ASSERT_NE(quick, nullptr);
  auto *title = quick->rootObject()->findChild<QQuickItem *>("myDayDateTitle");
  ASSERT_NE(title, nullptr);
  EXPECT_EQ(title->property("text").toString(), page.bridge()->dateTitle());
  EXPECT_FALSE(page.bridge()->dateTitle().isEmpty());

  page.bridge()->nextDay();
  QTest::qWait(20);
  EXPECT_EQ(title->property("text").toString(), page.bridge()->dateTitle());
  EXPECT_TRUE(gWarnings.isEmpty()) << gWarnings.join("\n").toStdString();
}

TEST_F(MyDayPageTest, PopulatedDayRendersWithoutWarnings) {
  // The page uses the real clock (no injection point): capture "today" once.
  const QDate today = QDateTime::currentDateTime().date();
  model->addEvent(workEvent(today, 9));
  model->addEvent(workEvent(today, 14));
  model->addEvent(workEvent(today, 19));
  pcm::my_day::MyDayPage page(db, model.get(), nullptr);
  page.resize(1100, 700);
  page.show();
  page.bridge()->addTodo("General todo", 0, 0);
  page.bridge()->setGroupByMeeting(true);
  QTest::qWait(50);
  auto *quick = quickOf(page);
  ASSERT_NE(quick, nullptr);
  ASSERT_EQ(quick->status(), QQuickWidget::Ready);
  EXPECT_EQ(page.bridge()->timeline().size(), 3);
  EXPECT_EQ(page.bridge()->todoTotal(), 1);

  // Exercise the add form and the unpaid popup so their bindings are evaluated too.
  auto *root = quick->rootObject();
  auto *todoColumn = root->findChild<QQuickItem *>("todoColumn");
  ASSERT_NE(todoColumn, nullptr);
  QMetaObject::invokeMethod(todoColumn, "startAdding");
  auto *popup = root->findChild<QObject *>("eventListPopup");
  ASSERT_NE(popup, nullptr);
  QMetaObject::invokeMethod(popup, "openWith", Q_ARG(QVariant, "Unpaid"),
                            Q_ARG(QVariant, page.bridge()->unpaidList()));
  QTest::qWait(50);
  EXPECT_TRUE(gWarnings.isEmpty()) << gWarnings.join("\n").toStdString();
}

TEST_F(MyDayPageTest, TodoCanBeRenamedWithAnExplicitActionAndFocusLoss) {
  pcm::my_day::MyDayPage page(db, model.get(), nullptr);
  page.resize(1100, 700);
  page.show();
  page.bridge()->addTodo("Original", 0, 0);
  QTest::qWait(50);
  auto *quick = quickOf(page);
  const auto findVisual = [](auto &&self, QQuickItem *item, const QString &name) -> QQuickItem * {
    if (item->objectName() == name) return item;
    for (auto *child : item->childItems())
      if (auto *found = self(self, child, name)) return found;
    return nullptr;
  };
  auto *action = findVisual(findVisual, quick->rootObject(), "todoRename");
  ASSERT_NE(action, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(action, "clicked"));
  QTest::qWait(20);
  auto *editor = findVisual(findVisual, quick->rootObject(), "todoTitleEditor");
  ASSERT_NE(editor, nullptr);
  ASSERT_TRUE(editor->isVisible());
  editor->setProperty("text", "Renamed");
  quick->rootObject()->forceActiveFocus();
  QTest::qWait(30);
  EXPECT_EQ(page.bridge()->todoGroups().first().toMap()["todos"].toList().first().toMap()["title"].toString(), "Renamed");
}

} // namespace

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("QT_QUICK_BACKEND", "software");
  QTemporaryDir settingsDir;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
  QApplication::setOrganizationName("SessioTest");
  QApplication::setApplicationName("MyDayPageTest");
  gPreviousHandler = qInstallMessageHandler(captureHandler);
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
