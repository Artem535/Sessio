#include "event_info.h"
#include "month_calendar_widget.h"
#include "app_settings.h"
#include <oclero/qlementine/widgets/Switch.hpp>
#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <QApplication>
#include <QAbstractButton>
#include <QFormLayout>
#include <QLineEdit>
#include <QSettings>
#include <QPointer>
#include <QToolButton>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QTimer>
#include <QDialog>
#include <QScrollBar>
#include <QScrollArea>
#include <QStackedWidget>
#include <QLabel>
#include <QHBoxLayout>
#include <QImage>
#include "rounded_calendar_widget.h"
#include "month_picker_widget.h"
#include <array>
#include <cmath>
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
TEST_F(CalendarLayoutTest, TranscriptButtonHiddenWhenCountIsZero) {
  model->addEvent(appointment(QDate::currentDate()));
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.setTranscriptCountProvider([](int64_t) { return 0; });
  page.findChild<MonthCalendarWidget *>()->eventSelected(model->events().first());
  auto *details = page.findChild<QEventDetailsWidget *>("calendarInspector");
  ASSERT_NE(details, nullptr);
  auto *button = details->findChild<QPushButton *>("openTranscript");
  ASSERT_NE(button, nullptr);
  EXPECT_FALSE(button->isVisibleTo(details));
}
TEST_F(CalendarLayoutTest, TranscriptButtonShownAndRelaysEventId) {
  const auto id = model->addEvent(appointment(QDate::currentDate()));
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.setTranscriptCountProvider([id](int64_t eventId) { return eventId == id ? 2 : 0; });
  page.findChild<MonthCalendarWidget *>()->eventSelected(model->events().first());
  auto *details = page.findChild<QEventDetailsWidget *>("calendarInspector");
  ASSERT_NE(details, nullptr);
  auto *button = details->findChild<QPushButton *>("openTranscript");
  ASSERT_NE(button, nullptr);
  EXPECT_TRUE(button->isVisibleTo(details));
  QSignalSpy requested(&page, &QEventInfoPage::openTranscriptRequested);
  button->click();
  ASSERT_EQ(requested.count(), 1);
  EXPECT_EQ(requested.first().first().toLongLong(), id);
}
TEST_F(CalendarLayoutTest, ReloadSelectedDayReloadsFromModelAndRefreshesTranscriptCount) {
  const auto id = model->addEvent(appointment(QDate::currentDate()));
  QEventInfoPage page(model.get(), nullptr, nullptr);
  int count = 1;
  page.setTranscriptCountProvider([&](int64_t) { return count; });
  page.findChild<MonthCalendarWidget *>()->eventSelected(model->events().first());
  QSignalSpy loaded(model.get(), &QTimelineModel::eventsLoaded);
  count = 0;
  auto event = model->events().first();
  event.name = "Changed directly in database";
  db->update_event(event);
  page.reloadSelectedDay();
  EXPECT_GE(loaded.count(), 1);
  auto *details = page.findChild<QEventDetailsWidget *>("calendarInspector");
  ASSERT_NE(details, nullptr);
  EXPECT_EQ(details->currentEvent()->getId(), id);
  EXPECT_EQ(details->currentEvent()->getTitle(), "Changed directly in database");
  EXPECT_FALSE(details->findChild<QPushButton *>("openTranscript")->isVisibleTo(details));
}
TEST_F(CalendarLayoutTest, ShowEventOnDaySelectsTheEventWithoutOpeningTheEditor) {
  const auto id = model->addEvent(appointment(QDate::currentDate()));
  QEventInfoPage page(model.get(), nullptr, nullptr);
  bool editorOpened = false;
  // An editor dialog would run its own modal loop; close it from inside.
  QTimer::singleShot(0, [&editorOpened] {
    if (auto *modal = QApplication::activeModalWidget()) {
      editorOpened = true;
      modal->close();
    }
  });
  page.showEventOnDay(id, QDateTime(QDate::currentDate(), QTime(0, 0)).toMSecsSinceEpoch());
  QCoreApplication::processEvents();
  EXPECT_FALSE(editorOpened);
  auto *details = page.findChild<QEventDetailsWidget *>("calendarInspector");
  ASSERT_NE(details, nullptr);
  EXPECT_EQ(details->currentEvent()->getId(), id);
}
TEST_F(CalendarLayoutTest, SwitchKeepsItsNaturalWidthNextToItsLabels) {
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.resize(1500, 900);
  page.show();
  QApplication::processEvents();
  auto *toggle = page.findChild<oclero::qlementine::Switch *>("calendarViewSwitch");
  ASSERT_NE(toggle, nullptr);
  EXPECT_EQ(toggle->sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
  EXPECT_LE(toggle->width(), toggle->sizeHint().width() + 2);
}
TEST_F(CalendarLayoutTest, InspectorKeepsNaturalHeightInATallPanel) {
  model->addEvent(appointment(QDate::currentDate()));
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.resize(1500, 900);
  page.show();
  QApplication::processEvents();
  page.findChild<MonthCalendarWidget *>()->eventSelected(model->events().first());
  QApplication::processEvents();
  auto *details = page.findChild<QEventDetailsWidget *>("calendarInspector");
  ASSERT_NE(details, nullptr);
  EXPECT_LE(details->height(), details->sizeHint().height() + 2);
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
  auto *card = page.findChild<RoundedCalendarWidget *>();
  ASSERT_NE(card, nullptr);
  emit card->clicked(QDate::currentDate().addDays(1));
  EXPECT_EQ(page.findChild<QEventDetailsWidget *>("calendarInspector"), nullptr);
  EXPECT_TRUE(model->events().isEmpty());
  emit card->clicked(QDate::currentDate());
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
  std::array<QColor, 2> calendarBase;
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
                                 : page.findChild<QStackedWidget *>("calendarCenterStack")->currentWidget();
      EXPECT_TRUE(calendar->isVisible());
      EXPECT_GT(calendar->width(), 350);
      EXPECT_GT(calendar->height(), 350);
      auto *scroll = page.findChild<QScrollArea *>("calendarInspectorScroll");
      EXPECT_TRUE(scroll->isVisible());
      EXPECT_EQ(scroll->horizontalScrollBar()->maximum(), 0);
      EXPECT_TRUE(page.findChild<QuickSlotsWidget *>()->isVisibleTo(&page));
      // The page palette (not the application one) must reach the calendar and
      // the inspector, and text must stay readable against the background.
      EXPECT_EQ(calendar->palette().color(QPalette::Base), palette.color(QPalette::Base));
      auto *inspector = page.findChild<QEventDetailsWidget *>("calendarInspector");
      ASSERT_NE(inspector, nullptr);
      const auto inspectorPalette = inspector->palette();
      EXPECT_EQ(inspectorPalette.color(QPalette::Window), palette.color(QPalette::Window));
      EXPECT_GT(std::abs(inspectorPalette.color(QPalette::WindowText).lightness() -
                         inspectorPalette.color(QPalette::Window).lightness()), 150);
      EXPECT_GT(std::abs(calendar->palette().color(QPalette::Text).lightness() -
                         calendar->palette().color(QPalette::Base).lightness()), 150);
      calendarBase[dark] = calendar->palette().color(QPalette::Base);
    }
  }
  EXPECT_LT(calendarBase[1].lightness(), calendarBase[0].lightness() - 100);
}
TEST_F(CalendarLayoutTest, MonthChipsFollowEventColorSettingsAfterPageRefresh) {
  auto event = appointment(QDate::currentDate());
  event.is_work_event = true;
  model->addEvent(event);
  pcm::app_settings::setWorkEventColor(QColor(20, 160, 60));
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.setMonthView(true);
  page.resize(1500, 900);
  page.show();
  QApplication::processEvents();
  auto chips = page.findChildren<QPushButton *>("monthEvent");
  ASSERT_FALSE(chips.isEmpty());
  EXPECT_EQ(chips.first()->property("chipFill").value<QColor>(), QColor(20, 160, 60));
  pcm::app_settings::setWorkEventColor(QColor(200, 30, 30));
  page.refreshAppearance();
  chips = page.findChildren<QPushButton *>("monthEvent");
  ASSERT_FALSE(chips.isEmpty());
  EXPECT_EQ(chips.first()->property("chipFill").value<QColor>(), QColor(200, 30, 30));
}

TEST_F(CalendarLayoutTest, NoOpRefreshKeepsTheInspectorWidgetAndChangedEventUpdatesIt) {
  const auto id = model->addEvent(appointment(QDate::currentDate()));
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.setMonthView(true);
  page.findChild<MonthCalendarWidget *>()->eventSelected(model->events().first());
  QPointer<QEventDetailsWidget> first = page.findChild<QEventDetailsWidget *>("calendarInspector");
  ASSERT_NE(first, nullptr);
  model->loadEventsForDay(QDate::currentDate());
  page.refreshAppearance();
  EXPECT_EQ(page.findChild<QEventDetailsWidget *>("calendarInspector"), first.data());
  page.setMonthView(false); // day mode: refreshes keep it as well
  model->loadEventsForDay(QDate::currentDate());
  page.refreshAppearance();
  EXPECT_EQ(page.findChild<QEventDetailsWidget *>("calendarInspector"), first.data());
  page.setMonthView(true);
  EXPECT_EQ(page.findChild<QEventDetailsWidget *>("calendarInspector"), first.data());
  auto changed = model->events().first();
  changed.name = "Moved on";
  changed.start_date = *changed.start_date + 1800000;
  changed.end_date = *changed.end_date + 1800000;
  model->updateEvent(changed);
  auto *second = page.findChild<QEventDetailsWidget *>("calendarInspector");
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second->currentEvent()->getId(), id);
  EXPECT_EQ(second->currentEvent()->getTitle(), "Moved on");
  EXPECT_EQ(second->currentEvent()->toEvent().start_date, changed.start_date);
}
TEST_F(CalendarLayoutTest, NoOpRefreshKeepsTheInspectorOfAVirtualOccurrence) {
  const auto date = QDate::currentDate();
  ASSERT_GT(model->addEventSeries(appointment(date), 0, "FREQ=DAILY", std::nullopt), 0);
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.setMonthView(true);
  page.findChild<MonthCalendarWidget *>()->eventSelected(model->events().first());
  QPointer<QEventDetailsWidget> first = page.findChild<QEventDetailsWidget *>("calendarInspector");
  ASSERT_NE(first, nullptr);
  ASSERT_TRUE(first->currentEvent()->toEvent().is_virtual_occurrence);
  EXPECT_EQ(first->recurrenceRule(), "FREQ=DAILY;INTERVAL=1");
  model->loadEventsForDay(date);
  page.refreshAppearance();
  EXPECT_EQ(page.findChild<QEventDetailsWidget *>("calendarInspector"), first.data());
}
TEST_F(CalendarLayoutTest, SeriesRuleChangeRebuildsTheInspectorWhenTheOccurrenceIsUnchanged) {
  // A materialized occurrence is stored separately from its series, so editing
  // only the series rule leaves the occurrence data byte-identical.
  const auto date = QDate::currentDate();
  const auto seriesId = model->addEventSeries(appointment(date), 0, "FREQ=DAILY", std::nullopt);
  ASSERT_GT(seriesId, 0);
  const auto range = model->eventsForRange(date, date);
  ASSERT_FALSE(range.isEmpty());
  auto materialized = range.first();
  materialized.id = -1;
  ASSERT_GT(model->addEvent(materialized), 0);
  model->loadEventsForDay(date);
  ASSERT_FALSE(model->events().isEmpty());
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.setMonthView(true);
  page.findChild<MonthCalendarWidget *>()->eventSelected(model->events().first());
  QPointer<QEventDetailsWidget> first = page.findChild<QEventDetailsWidget *>("calendarInspector");
  ASSERT_NE(first, nullptr);
  ASSERT_FALSE(first->currentEvent()->toEvent().is_virtual_occurrence);
  ASSERT_TRUE(first->currentEvent()->toEvent().series_id.has_value());
  EXPECT_FALSE(first->recurrenceUntilMs().has_value());
  model->loadEventsForDay(date);
  page.refreshAppearance();
  EXPECT_EQ(page.findChild<QEventDetailsWidget *>("calendarInspector"), first.data());
  const auto until = QDateTime(date.addDays(30), QTime(23, 59, 59)).toMSecsSinceEpoch();
  ASSERT_TRUE(model->updateEventSeries(appointment(date), seriesId, 0, "FREQ=DAILY", until));
  model->loadEventsForDay(date); // the model reloads after a series edit
  auto *second = page.findChild<QEventDetailsWidget *>("calendarInspector");
  ASSERT_NE(second, nullptr);
  EXPECT_TRUE(second->recurrenceUntilMs().has_value());
  model->loadEventsForDay(date);
  page.refreshAppearance();
  EXPECT_EQ(page.findChild<QEventDetailsWidget *>("calendarInspector"), second);
}
TEST_F(CalendarLayoutTest, EventsAddedInDayModeAppearWhenSwitchingToMonth) {
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.setMonthView(true);
  auto *month = page.findChild<MonthCalendarWidget *>();
  const auto before = month->findChildren<QAbstractButton *>().size();
  page.setMonthView(false);
  model->addEvent(appointment(QDate::currentDate()));
  page.setMonthView(true);
  EXPECT_GT(month->findChildren<QAbstractButton *>().size(), before);
}
namespace {
QToolButton *dateButtonFor(MonthCalendarWidget *month, const QDate &date) {
  for (auto *button : month->findChildren<QToolButton *>("monthDate")) {
    if (button->property("calendarDate").toDate() == date) {
      return button;
    }
  }
  return nullptr;
}
} // namespace

TEST_F(CalendarLayoutTest, ActivatingADateCellByKeyboardKeepsFocusOnThatDate) {
  model->addEvent(appointment(QDate::currentDate()));
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.resize(1000, 760);
  page.show();
  ASSERT_TRUE(QTest::qWaitForWindowActive(&page));
  page.setMonthView(true);
  auto *month = page.findChild<MonthCalendarWidget *>();
  const auto today = QDate::currentDate();
  const auto target = QDate(today.year(), today.month(), today.day() == 15 ? 16 : 15);
  auto *button = dateButtonFor(month, target);
  ASSERT_NE(button, nullptr);
  button->setFocus();
  ASSERT_EQ(QApplication::focusWidget(), button);
  QTest::keyClick(button, Qt::Key_Space);
  QApplication::processEvents();
  auto *now = dateButtonFor(month, target);
  ASSERT_NE(now, nullptr);
  EXPECT_TRUE(now->isChecked());
  EXPECT_EQ(QApplication::focusWidget(), now);
}

TEST_F(CalendarLayoutTest, ActivatingAnEventButtonByKeyboardKeepsFocusOnThatEvent) {
  const auto today = QDate::currentDate();
  model->addEvent(appointment(today));
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.resize(1000, 760);
  page.show();
  ASSERT_TRUE(QTest::qWaitForWindowActive(&page));
  page.setMonthView(true);
  auto *month = page.findChild<MonthCalendarWidget *>();
  auto buttons = month->findChildren<QPushButton *>("monthEvent");
  ASSERT_EQ(buttons.size(), 1);
  buttons.first()->setFocus();
  ASSERT_EQ(QApplication::focusWidget(), buttons.first());
  QTest::keyClick(buttons.first(), Qt::Key_Space);
  QApplication::processEvents();
  buttons = month->findChildren<QPushButton *>("monthEvent");
  ASSERT_EQ(buttons.size(), 1);
  EXPECT_EQ(QApplication::focusWidget(), buttons.first());
}

TEST_F(CalendarLayoutTest, RefreshWithUnchangedMonthAndEventsKeepsTheCellWidgets) {
  model->addEvent(appointment(QDate::currentDate()));
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.resize(1000, 760);
  page.show();
  page.setMonthView(true);
  auto *month = page.findChild<MonthCalendarWidget *>();
  const auto dates = month->findChildren<QToolButton *>("monthDate");
  const auto events = month->findChildren<QPushButton *>("monthEvent");
  ASSERT_FALSE(dates.isEmpty());
  ASSERT_EQ(events.size(), 1);
  const auto builds = month->rebuildCount();
  page.setMonthView(true);
  QApplication::processEvents();
  EXPECT_EQ(month->rebuildCount(), builds);
  EXPECT_EQ(month->findChildren<QToolButton *>("monthDate"), dates);
  EXPECT_EQ(month->findChildren<QPushButton *>("monthEvent"), events);
}

TEST_F(CalendarLayoutTest, MonthNavigationBuildsTheGridOncePerRefresh) {
  model->addEvent(appointment(QDate::currentDate()));
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.resize(1000, 760);
  page.show();
  page.setMonthView(true);
  auto *month = page.findChild<MonthCalendarWidget *>();
  const auto builds = month->rebuildCount();
  month->dateSelected(QDate::currentDate().addMonths(1));
  QApplication::processEvents();
  EXPECT_EQ(month->rebuildCount(), builds + 1);
}

TEST_F(CalendarLayoutTest, TogglingViewModeWritesNoSettingsOrData) {
  model->addEvent(appointment(QDate::currentDate()));
  QSettings settings;
  settings.sync();
  const auto keys = settings.allKeys();
  QEventInfoPage page(model.get(), nullptr, nullptr);
  const auto events = model->events().size();
  page.setMonthView(true);
  page.setMonthView(false);
  settings.sync();
  EXPECT_EQ(settings.allKeys(), keys);
  EXPECT_EQ(model->events().size(), events);
  EXPECT_EQ(model->eventsForRange(QDate::currentDate(), QDate::currentDate()).size(), events);
}
TEST_F(CalendarLayoutTest, LeavingInspectorModeRestoresTheFormLayout) {
  QEventDetailsWidget details;
  auto *actions = details.findChild<QPushButton *>("openMeetingButton")->parentWidget();
  auto *series = details.findChild<QWidget *>("seriesCallPanel");
  ASSERT_NE(series, nullptr);
  auto *form = details.findChild<QFormLayout *>();
  ASSERT_NE(form, nullptr);
  const auto seriesActions = [series] {
    return static_cast<QBoxLayout *>(series->layout()->itemAt(2)->layout());
  };
  const auto actionsLayout = [actions] { return static_cast<QBoxLayout *>(actions->layout()); };
  const auto position = [form](QWidget *widget) {
    int row = -1;
    QFormLayout::ItemRole role;
    form->getWidgetPosition(widget, &row, &role);
    return std::pair(row, role);
  };
  const auto actionsPosition = position(actions);
  const auto seriesPosition = position(series);
  ASSERT_GE(actionsPosition.first, 0);
  ASSERT_GE(seriesPosition.first, 0);
  EXPECT_EQ(actionsLayout()->direction(), QBoxLayout::LeftToRight);
  details.setInspectorMode(true);
  EXPECT_EQ(position(actions).first, -1);
  EXPECT_EQ(position(series).first, -1);
  EXPECT_EQ(actionsLayout()->direction(), QBoxLayout::TopToBottom);
  EXPECT_EQ(seriesActions()->direction(), QBoxLayout::TopToBottom);
  details.setInspectorMode(false);
  EXPECT_EQ(position(actions), actionsPosition);
  EXPECT_EQ(position(series), seriesPosition);
  EXPECT_EQ(actionsLayout()->direction(), QBoxLayout::LeftToRight);
  EXPECT_EQ(seriesActions()->direction(), QBoxLayout::LeftToRight);
  EXPECT_EQ(actions->parentWidget(), form->parentWidget());
}
TEST_F(CalendarLayoutTest, NullLoadEventKeepsDialogModeUntouched) {
  QEventDetailsWidget dialog; // not an inspector
  QEventItem item(appointment(QDate::currentDate()));
  dialog.loadEvent(&item);
  ASSERT_EQ(dialog.currentEvent(), &item);
  dialog.loadEvent(nullptr);
  EXPECT_EQ(dialog.currentEvent(), &item);
}
TEST_F(CalendarLayoutTest, SwitchSwapsTheCalendarCardAndTheCentrePage) {
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.resize(1100, 720);
  page.show();
  QApplication::processEvents();
  auto *card = page.findChild<QStackedWidget *>("calendarCardStack");
  auto *centre = page.findChild<QStackedWidget *>("calendarCenterStack");
  ASSERT_NE(card, nullptr);
  ASSERT_NE(centre, nullptr);
  EXPECT_EQ(card->currentWidget(), page.findChild<RoundedCalendarWidget *>());
  EXPECT_EQ(centre->currentWidget(), page.findChild<QTimelineWidget *>());
  const auto cardGeometry = card->geometry();
  page.setMonthView(true);
  QApplication::processEvents();
  EXPECT_EQ(card->currentWidget(), page.findChild<MonthPickerWidget *>());
  EXPECT_TRUE(page.findChild<MonthCalendarWidget *>()->isVisible());
  EXPECT_EQ(card->geometry(), cardGeometry); // the card keeps its size and place
  EXPECT_TRUE(page.findChild<QuickSlotsWidget *>()->isVisible());
  EXPECT_TRUE(page.headerControls()->isAncestorOf(page.findChild<QWidget *>("calendarViewSwitch")));
  EXPECT_TRUE(page.headerControls()->isAncestorOf(page.findChild<QWidget *>("newCalendarEvent")));
}
TEST_F(CalendarLayoutTest, MonthPickerTileChangesTheDisplayedMonthAndYearArrowsBrowse) {
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.setMonthView(true);
  auto *picker = page.findChild<MonthPickerWidget *>();
  auto *month = page.findChild<MonthCalendarWidget *>();
  ASSERT_NE(picker, nullptr);
  const auto year = QDate::currentDate().year();
  page.findChild<QToolButton *>("monthPickerNextYear")->click();
  EXPECT_EQ(picker->shownYear(), year + 1);
  EXPECT_EQ(picker->displayedMonth().year(), year); // browsing does not move the grid
  QToolButton *march = nullptr;
  for (auto *tile : picker->findChildren<QToolButton *>("monthPickerTile")) {
    if (tile->property("calendarMonth").toInt() == 3) march = tile;
  }
  ASSERT_NE(march, nullptr);
  EXPECT_FALSE(march->text().isEmpty());
  march->click();
  EXPECT_EQ(picker->displayedMonth(), QDate(year + 1, 3, 1));
  EXPECT_EQ(month->firstVisibleDate(), MonthCalendarWidget::visibleRange(QDate(year + 1, 3, 1)).first);
}
TEST_F(CalendarLayoutTest, InfoPanelSwapsBetweenDaySummaryAndInspector) {
  const auto id = model->addEvent(appointment(QDate::currentDate()));
  QEventInfoPage page(model.get(), nullptr, nullptr);
  auto *info = page.findChild<QStackedWidget *>("calendarInfoStack");
  ASSERT_NE(info, nullptr);
  EXPECT_EQ(info->currentIndex(), 0);
  EXPECT_TRUE(info->widget(0)->findChild<DaySummaryWidget *>() != nullptr);
  page.findChild<QTimelineWidget *>()->eventSelected(id);
  EXPECT_EQ(info->currentIndex(), 1);
  EXPECT_NE(page.findChild<QEventDetailsWidget *>("calendarInspector"), nullptr);
  page.findChild<QPushButton *>("inspectorBackToDay")->click();
  EXPECT_EQ(info->currentIndex(), 0);
  EXPECT_EQ(page.findChild<QEventDetailsWidget *>("calendarInspector"), nullptr);
  page.findChild<QTimelineWidget *>()->eventSelected(id);
  ASSERT_EQ(info->currentIndex(), 1);
  model->removeEvent(id); // deleting the selected meeting returns to the day
  EXPECT_EQ(info->currentIndex(), 0);
  const auto other = model->addEvent(appointment(QDate::currentDate()));
  page.findChild<MonthCalendarWidget *>()->eventSelected(model->events().first());
  EXPECT_EQ(info->currentIndex(), 1);
  emit page.findChild<RoundedCalendarWidget *>()->clicked(QDate::currentDate().addDays(1));
  EXPECT_EQ(info->currentIndex(), 0); // choosing a day shows the day again
  Q_UNUSED(other);
}
TEST_F(CalendarLayoutTest, InspectorShowsLabelledFieldsAndFullWidthButtons) {
  auto event = appointment(QDate::currentDate());
  event.is_online = true;
  event.provider_kind = "ExternalUrl";
  event.meeting_url = "https://example.test/meeting";
  model->addEvent(event);
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.resize(1500, 900);
  page.show();
  page.findChild<QTimelineWidget *>()->eventSelected(model->events().first().id);
  QApplication::processEvents();
  auto *details = page.findChild<QEventDetailsWidget *>("calendarInspector");
  ASSERT_NE(details, nullptr);
  for (const char *name : {"inspectorDate", "inspectorTime", "inspectorRepeat", "inspectorFormat"}) {
    auto *caption = details->findChild<QLabel *>(QString(name) + "Caption");
    auto *value = details->findChild<QLabel *>(name);
    ASSERT_NE(caption, nullptr) << name;
    ASSERT_NE(value, nullptr) << name;
    EXPECT_FALSE(caption->text().isEmpty()) << name;
    EXPECT_FALSE(value->text().isEmpty()) << name;
    EXPECT_LT(caption->geometry().bottom(), value->geometry().top() + 1) << name; // caption above value
  }
  const auto open = details->findChild<QPushButton *>("openMeetingButton");
  const auto copy = details->findChild<QPushButton *>("copyMeetingInviteButton");
  const auto edit = details->findChild<QPushButton *>("mChangeButton");
  ASSERT_TRUE(open->isVisibleTo(details));
  EXPECT_EQ(open->width(), edit->width());
  EXPECT_EQ(copy->width(), edit->width());
  EXPECT_GT(edit->width(), details->width() - 60); // full width of the card
  EXPECT_TRUE(copy->isDefault());                  // primary action of an online meeting
}
TEST_F(CalendarLayoutTest, FirstMeetingOfTheDayIsScrolledIntoView) {
  auto event = appointment(QDate::currentDate());
  event.start_date = QDateTime(QDate::currentDate(), QTime(16, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  event.end_date = *event.start_date + 3600000;
  model->addEvent(event);
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.resize(1100, 520);
  page.show();
  QTest::qWait(50);
  auto *view = page.findChild<QEventView *>();
  ASSERT_NE(view, nullptr);
  const auto bar = view->verticalScrollBar();
  ASSERT_GT(bar->maximum(), 0);
  EXPECT_LE(bar->value(), 16 * 60);
  EXPECT_GE(bar->value() + view->viewport()->height(), 16 * 60);
}
TEST_F(CalendarLayoutTest, HeaderControlsSitTogetherAtTheRightOfTheirHost) {
  QEventInfoPage page(model.get(), nullptr, nullptr);
  QWidget host;
  auto *layout = new QHBoxLayout(&host);
  layout->setContentsMargins(19, 0, 19, 0);
  page.headerControls()->setParent(&host);
  layout->addWidget(page.headerControls());
  host.resize(1000, 40);
  host.show();
  QApplication::processEvents();
  auto *toggle = page.headerControls()->findChild<QWidget *>("calendarViewSwitch");
  auto *button = page.headerControls()->findChild<QWidget *>("newCalendarEvent");
  const auto toggleLeft = toggle->mapTo(&host, QPoint(0, 0)).x();
  const auto buttonRight = button->mapTo(&host, QPoint(button->width(), 0)).x();
  EXPECT_GE(buttonRight, host.width() - 19 - 2);
  EXPECT_GT(toggleLeft, host.width() - 450); // right next to the button, not at the left
}
TEST_F(CalendarLayoutTest, MonthPickerUsesConsistentThreeLetterNames) {
  MonthPickerWidget picker;
  for (auto *tile : picker.findChildren<QToolButton *>("monthPickerTile")) {
    EXPECT_EQ(tile->text().size(), 3) << tile->text().toStdString();
    EXPECT_FALSE(tile->text().endsWith('.'));
  }
}
TEST_F(CalendarLayoutTest, InfoPanelIsOneCardWithTheSelectedDateAsHeading) {
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.resize(1275, 900);
  page.show();
  page.setMonthView(true);
  QApplication::processEvents();
  auto *card = page.findChild<QWidget *>("infoPanelCard");
  auto *info = page.findChild<QStackedWidget *>("calendarInfoStack");
  ASSERT_NE(card, nullptr);
  EXPECT_TRUE(card->isAncestorOf(info));
  auto *heading = page.findChild<QLabel *>("daySummaryDate");
  ASSERT_NE(heading, nullptr);
  const auto date = QDate::currentDate().addDays(1);
  auto *month = page.findChild<MonthCalendarWidget *>();
  emit month->dateSelected(date);
  auto expected = QLocale().toString(date, QLocale::LongFormat);
  expected[0] = expected.at(0).toUpper();
  EXPECT_EQ(heading->text(), expected);
  page.findChild<QToolButton *>("monthPickerNextYear")->click();
  for (auto *tile : page.findChildren<QToolButton *>("monthPickerTile"))
    if (tile->property("calendarMonth").toInt() == 5) tile->click();
  EXPECT_TRUE(heading->text().contains(QLocale().standaloneMonthName(5, QLocale::LongFormat)) ||
              heading->text().contains(QLocale().monthName(5, QLocale::LongFormat)));
}
TEST_F(CalendarLayoutTest, MonthGridBottomMeetsTheLeftColumnBottomAtAnySize) {
  QEventInfoPage page(model.get(), nullptr, nullptr);
  auto *month = page.findChild<MonthCalendarWidget *>();
  auto *left = page.findChild<QWidget *>("calendarLeftColumn");
  auto *day = page.findChild<QTimelineWidget *>();
  const auto bottomOf = [&page](QWidget *w) { return w->mapTo(&page, QPoint(0, w->height())).y(); };
  for (const auto &month6 : {QDate(2026, 10, 1), QDate(2026, 3, 1)}) { // 5 and 6 rows
    for (const auto size : {QSize(2000, 1200), QSize(1920, 1060), QSize(1500, 880), QSize(1275, 1375), QSize(3000, 2000)}) {
      page.setMonthView(false);
      QMetaObject::invokeMethod(&page, "onCalendarClicked", Q_ARG(QDate, month6));
      page.resize(size);
      page.show();
      QApplication::processEvents();
      EXPECT_NEAR(bottomOf(day), bottomOf(left), 1) << size.width() << "x" << size.height();
      page.setMonthView(true);
      QApplication::processEvents();
      EXPECT_EQ(month->firstVisibleDate(), MonthCalendarWidget::visibleRange(month6).first);
      EXPECT_NEAR(bottomOf(month), bottomOf(left), 1)
          << month6.toString().toStdString() << " " << size.width() << "x" << size.height();
    }
  }
}
TEST_F(CalendarLayoutTest, DialogFormKeepsTheBaseInsetAndButtonOrder) {
  QEventDetailsWidget details;
  auto *form = details.findChild<QFormLayout *>();
  ASSERT_NE(form, nullptr);
  // Base (bc37801): the form was a nested layout of the vertical layout, i.e. no own margins.
  EXPECT_EQ(form->contentsMargins(), QMargins(0, 0, 0, 0));
  auto *open = details.findChild<QPushButton *>("openMeetingButton");
  auto *url = details.findChild<QPushButton *>("copyMeetingUrlButton");
  auto *invite = details.findChild<QPushButton *>("copyMeetingInviteButton");
  auto *passcode = details.findChild<QPushButton *>("copyMeetingPasscodeButton");
  auto *layout = static_cast<QBoxLayout *>(open->parentWidget()->layout());
  const auto index = [layout](QWidget *w) { return layout->indexOf(w); };
  const auto dialogOrder = [&] {
    EXPECT_LT(index(open), index(url));
    EXPECT_LT(index(url), index(invite));
    EXPECT_LT(index(invite), index(passcode));
  };
  dialogOrder(); // original: Open, Copy link, Copy invite, Copy passcode
  details.setInspectorMode(true);
  EXPECT_LT(index(invite), index(url));   // inspector: Copy invite first ...
  EXPECT_LT(index(url), index(passcode));
  EXPECT_LT(index(passcode), index(open)); // ... Open last
  details.setInspectorMode(false);
  dialogOrder();
}
TEST_F(CalendarLayoutTest, TimelineScrollsOnlyWhenTheDisplayedDateChanges) {
  auto event = appointment(QDate::currentDate());
  event.start_date = QDateTime(QDate::currentDate(), QTime(16, 0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
  event.end_date = *event.start_date + 3600000;
  model->addEvent(event);
  QEventInfoPage page(model.get(), nullptr, nullptr);
  page.resize(1100, 520);
  page.show();
  QTest::qWait(50);
  auto *view = page.findChild<QEventView *>();
  ASSERT_NE(view, nullptr);
  auto *bar = view->verticalScrollBar();
  ASSERT_GT(bar->value(), 0); // first load scrolled to the 16:00 meeting
  bar->setValue(0);           // the user scrolls away
  model->loadEventsForDay(QDate::currentDate()); // reload after save/delete/edit
  QTest::qWait(50);
  EXPECT_EQ(bar->value(), 0);
  model->loadEventsForDay(QDate::currentDate().addDays(1)); // other date, empty -> 08:00
  QTest::qWait(50);
  EXPECT_GT(bar->value(), 0);
}
} // namespace
int main(int argc, char **argv) {
  QTemporaryDir home;
  qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
  qputenv("HOME", home.path().toUtf8());
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("SessioCalendarLayoutTests");
  app.setStyle(new oclero::qlementine::QlementineStyle(&app));
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
