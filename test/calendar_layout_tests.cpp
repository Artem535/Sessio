#include "event_info.h"
#include "month_calendar_widget.h"
#include <oclero/qlementine/widgets/Switch.hpp>
#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <QApplication>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QTimer>
#include <QDialog>
#include <QScrollBar>
#include <gtest/gtest.h>

namespace {
DuckEvent appointment(QDate date) {
  DuckEvent event;
  event.name = "Appointment";
  event.start_date = QDateTime(date, QTime(10, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  event.end_date = *event.start_date + 3600000;
  event.duration = 3600;
  event.event_stat_id = 1;
  event.payment_stat_id = 1;
  return event;
}
class CalendarLayoutTest : public ::testing::Test {
protected:
  void SetUp() override {
    pcm::config::Config config{.db_conf = pcm::config::DatabaseConfig{
        .db_pth = Poco::Path(dir.path().toStdString())}};
    db = std::make_shared<pcm::database::Database>(config);
    model = std::make_unique<QTimelineModel>(db, nullptr);
  }
  QTemporaryDir dir;
  std::shared_ptr<pcm::database::Database> db;
  std::unique_ptr<QTimelineModel> model;
};
TEST_F(CalendarLayoutTest, RealSwitchChangesVisibleCalendarWithoutReloadingDay) {
  const auto id = model->addEvent(appointment(QDate::currentDate()));
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.resize(900, 680);
  page.show();
  QApplication::processEvents();
  auto *toggle = page.findChild<oclero::qlementine::Switch *>("calendarViewSwitch");
  ASSERT_NE(toggle, nullptr);
  EXPECT_EQ(toggle->accessibleName(), "Calendar view");
  auto *month = page.findChild<MonthCalendarWidget *>();
  auto *day = page.findChild<QTimelineWidget *>();
  ASSERT_NE(month, nullptr);
  QSignalSpy loaded(model.get(), &QTimelineModel::eventsLoaded);
  QTest::mouseClick(toggle, Qt::LeftButton);
  EXPECT_TRUE(month->isVisible());
  EXPECT_FALSE(day->isVisible());
  EXPECT_EQ(loaded.count(), 0);
  ASSERT_EQ(model->events().size(), 1);
  EXPECT_EQ(model->events().first().id, id);
  QTest::keyClick(toggle, Qt::Key_Space);
  EXPECT_TRUE(day->isVisible());
  EXPECT_FALSE(month->isVisible());
}
TEST_F(CalendarLayoutTest, MonthlySelectionOwnsReadOnlyDetailsAndRefreshClearsDeletedEvent) {
  const auto id = model->addEvent(appointment(QDate::currentDate()));
  QEventInfoPage page(model.get(), nullptr, nullptr);
  auto *month = page.findChild<MonthCalendarWidget *>();
  ASSERT_NE(month, nullptr);
  month->eventSelected(model->events().first());
  auto *details = page.findChild<QEventDetailsWidget *>("calendarInspector");
  ASSERT_NE(details, nullptr);
  ASSERT_NE(details->currentEvent(), nullptr);
  EXPECT_EQ(details->currentEvent()->getId(), id);
  EXPECT_FALSE(details->isInEditMode());
  EXPECT_FALSE(details->findChild<QLineEdit *>("mTitle")->isVisibleTo(details));
  EXPECT_FALSE(details->findChild<QPushButton *>("openMeetingButton")->isVisibleTo(details));
  auto changed = model->events().first();
  changed.name = "Changed appointment";
  model->updateEvent(changed);
  details = page.findChild<QEventDetailsWidget *>("calendarInspector");
  ASSERT_NE(details, nullptr);
  EXPECT_EQ(details->currentEvent()->getTitle(), "Changed appointment");
  model->removeEvent(id);
  EXPECT_EQ(page.findChild<QEventDetailsWidget *>("calendarInspector"), nullptr);
}
TEST_F(CalendarLayoutTest, OnlineInspectorRoutesStoredTarget) {
  auto event = appointment(QDate::currentDate());
  event.is_online = true;
  event.provider_kind = "LiveKit";
  event.meeting_ref = "meeting-original";
  model->addEvent(event);
  QEventInfoPage page(model.get(), nullptr, nullptr);
  auto *month = page.findChild<MonthCalendarWidget *>();
  ASSERT_NE(month, nullptr);
  month->eventSelected(model->events().first());
  auto *details = page.findChild<QEventDetailsWidget *>("calendarInspector");
  ASSERT_NE(details, nullptr);
  auto *open = details->findChild<QPushButton *>("openMeetingButton");
  ASSERT_NE(open, nullptr);
  EXPECT_TRUE(open->isVisibleTo(details));
  QSignalSpy requested(&page, &QEventInfoPage::openLiveKitMeetingRequested);
  open->click();
  ASSERT_EQ(requested.count(), 1);
  EXPECT_EQ(requested.first().first().toString(), "meeting-original");
}
TEST_F(CalendarLayoutTest, RecurrenceSelectionResolvesDayIdentityBeforeOpeningExistingEditor) {
  const auto date = QDate::currentDate();
  ASSERT_GT(model->addEventSeries(appointment(date.addDays(-14)), 0, "FREQ=DAILY", std::nullopt), 0);
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.setMonthView(true);
  auto *month = page.findChild<MonthCalendarWidget *>();
  const auto range = model->eventsForRange(date.addDays(-10), date);
  ASSERT_GT(range.size(), 5);
  const auto selected = range.first();
  ASSERT_NE(QDateTime::fromMSecsSinceEpoch(*selected.start_date).date(), date);
  month->eventSelected(selected);
  auto *details = page.findChild<QEventDetailsWidget *>("calendarInspector");
  ASSERT_NE(details, nullptr);
  ASSERT_EQ(model->events().size(), 1);
  // Virtual ids are deterministic per (series, day), so identity is checked by
  // series/original start below, not by id inequality.
  EXPECT_EQ(selected.series_id, model->events().first().series_id);
  EXPECT_EQ(details->currentEvent()->getId(), model->events().first().id);
  EXPECT_EQ(details->currentEvent()->toEvent().original_occurrence_start, selected.original_occurrence_start);
  page.setMonthView(false);
  page.setMonthView(true);
  details = page.findChild<QEventDetailsWidget *>("calendarInspector");
  ASSERT_NE(details, nullptr);
  bool editorSeen = false;
  QTimer::singleShot(0, &page, [&] {
    for (auto *dialog : page.findChildren<QDialog *>()) {
      auto *editor = dialog->findChild<QEventDetailsWidget *>();
      if (!editor) continue;
      editorSeen = true;
      EXPECT_TRUE(editor->isInEditMode());
      EXPECT_EQ(editor->currentEvent()->getId(), model->events().first().id);
      EXPECT_EQ(editor->currentEvent()->toEvent().original_occurrence_start, selected.original_occurrence_start);
      dialog->reject();
    }
  });
  details->findChild<QPushButton *>("mChangeButton")->click();
  EXPECT_TRUE(editorSeen);
}
TEST_F(CalendarLayoutTest, DaySelectionAndPeriodNavigationUseTheSharedInspector) {
  const auto id = model->addEvent(appointment(QDate::currentDate()));
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.findChild<QTimelineWidget *>()->eventSelected(id);
  auto *details = page.findChild<QEventDetailsWidget *>("calendarInspector");
  ASSERT_NE(details, nullptr);
  EXPECT_EQ(details->currentEvent()->getId(), id);
  page.findChild<QPushButton *>("nextCalendarPeriod")->click();
  EXPECT_EQ(page.findChild<QEventDetailsWidget *>("calendarInspector"), nullptr);
  EXPECT_TRUE(model->events().isEmpty());
  page.findChild<QPushButton *>("previousCalendarPeriod")->click();
  ASSERT_EQ(model->events().size(), 1);
  EXPECT_EQ(model->events().first().id, id);
}
TEST_F(CalendarLayoutTest, MaterializedMoveRefreshesTheOriginalOccurrenceSelection) {
  const auto date = QDate::currentDate();
  ASSERT_GT(model->addEventSeries(appointment(date), 0, "FREQ=DAILY", std::nullopt), 0);
  QEventInfoPage page(model.get(), nullptr, nullptr);
  auto *month = page.findChild<MonthCalendarWidget *>();
  month->eventSelected(model->events().first());
  auto moved = model->events().first();
  moved.id = -1;
  moved.start_date = *moved.start_date + 7200000;
  moved.end_date = *moved.end_date + 7200000;
  const auto id = model->addEvent(moved);
  ASSERT_GT(id, 0);
  auto *details = page.findChild<QEventDetailsWidget *>("calendarInspector");
  ASSERT_NE(details, nullptr);
  EXPECT_EQ(details->currentEvent()->getId(), id);
  EXPECT_EQ(details->currentEvent()->toEvent().start_date, moved.start_date);
  model->loadEventsForDay(date);
  model->removeEvent(id);
  EXPECT_EQ(page.findChild<QEventDetailsWidget *>("calendarInspector"), nullptr);
  EXPECT_TRUE(model->eventsForRange(date, date).isEmpty());
}
TEST_F(CalendarLayoutTest, InspectorCopiesInputAndNeverSavesItsHiddenEditor) {
  QEventDetailsWidget details;
  details.setInspectorMode(true);
  {
    QEventItem original(appointment(QDate::currentDate()));
    details.loadEvent(&original);
    EXPECT_NE(details.currentEvent(), &original);
  }
  ASSERT_NE(details.currentEvent(), nullptr);
  QSignalSpy saves(&details, &QEventDetailsWidget::provideEventSave);
  QSignalSpy edits(&details, &QEventDetailsWidget::editRequested);
  details.findChild<QPushButton *>("mChangeButton")->click();
  ASSERT_EQ(edits.count(), 1);
  EXPECT_FALSE(details.isInEditMode());
  ASSERT_TRUE(QMetaObject::invokeMethod(&details, "onApplyClicked"));
  EXPECT_EQ(saves.count(), 0);
  auto external = appointment(QDate::currentDate());
  external.is_online = true;
  external.provider_kind = "ExternalUrl";
  external.meeting_url = "https://example.test/meeting";
  QEventItem item(external);
  details.loadEvent(&item);
  auto *open = details.findChild<QPushButton *>("openMeetingButton");
  EXPECT_TRUE(open->isVisibleTo(&details));
  EXPECT_TRUE(open->isEnabled());
  item.setMeetingUrl("file:///not-a-meeting");
  details.loadEvent(&item);
  EXPECT_FALSE(open->isEnabled());
  item.setOnline(false);
  details.loadEvent(&item);
  EXPECT_FALSE(open->isVisibleTo(&details));
}
TEST_F(CalendarLayoutTest, SmallPageKeepsCalendarAndScrollingInspectorAccessibleInBothPalettes) {
  auto event = appointment(QDate::currentDate());
  event.name = std::string(300, 'W');
  model->addEvent(event);
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.resize(900, 640); // 1100x720 main window after navigation/header
  page.show();
  page.findChild<MonthCalendarWidget *>()->eventSelected(model->events().first());
  for (bool dark : {false, true}) {
    auto palette = QApplication::palette();
    palette.setColor(QPalette::Window, dark ? QColor("#202428") : QColor("#ffffff"));
    palette.setColor(QPalette::Base, dark ? QColor("#181c20") : QColor("#ffffff"));
    palette.setColor(QPalette::WindowText, dark ? Qt::white : Qt::black);
    palette.setColor(QPalette::Text, dark ? Qt::white : Qt::black);
    page.setPalette(palette);
    for (bool monthMode : {false, true}) {
      page.setMonthView(monthMode);
      QApplication::processEvents();
      EXPECT_LE(page.width(), 900);
      EXPECT_LE(page.height(), 640);
      auto *calendar = monthMode ? static_cast<QWidget *>(page.findChild<MonthCalendarWidget *>())
                                 : page.findChild<QWidget *>("calendarDayScroll");
      EXPECT_TRUE(calendar->isVisible());
      EXPECT_GT(calendar->width(), 350);
      EXPECT_GT(calendar->height(), 350);
      auto *scroll = page.findChild<QScrollArea *>("calendarInspectorScroll");
      EXPECT_TRUE(scroll->isVisible());
      EXPECT_EQ(scroll->horizontalScrollBar()->maximum(), 0);
      EXPECT_TRUE(page.findChild<QuickSlotsWidget *>()->isVisibleTo(page.findChild<QScrollArea *>("calendarDayScroll")));
      EXPECT_EQ(calendar->palette().color(QPalette::Base), palette.color(QPalette::Base));
    }
  }
}
} // namespace
int main(int argc, char **argv) {
  QTemporaryDir home;
  qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
  qputenv("HOME", home.path().toUtf8());
  QApplication app(argc, argv);
  app.setStyle(new oclero::qlementine::QlementineStyle(&app));
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
