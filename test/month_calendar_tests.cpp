#include "qtimeline_model.h"
#include "series_schedule_committer.h"
#include "month_calendar_widget.h"
#include "app_settings.h"

#include <QApplication>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QToolButton>
#include <QPushButton>
#include <QMenu>
#include <QTest>
#include <QImage>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <memory>

namespace {

qint64 localMs(const QDate &date, const QTime &time = QTime(0, 0)) {
  return QDateTime(date, time, QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
}

DuckEvent eventAt(const QDate &date, const QTime &time = QTime(9, 0), int seconds = 3600) {
  DuckEvent event;
  event.name = "Calendar appointment";
  event.start_date = localMs(date, time);
  event.end_date = *event.start_date + seconds * 1000LL;
  event.duration = seconds;
  event.event_stat_id = 1;
  event.payment_stat_id = 1;
  return event;
}

class MonthProjectionTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(dir.isValid());
    pcm::config::Config config{
        .db_conf = pcm::config::DatabaseConfig{
            .db_pth = Poco::Path(dir.path().toStdString())}};
    db = std::make_shared<pcm::database::Database>(config);
    model = std::make_unique<QTimelineModel>(db, nullptr);
  }

  QTemporaryDir dir;
  std::shared_ptr<pcm::database::Database> db;
  std::unique_ptr<QTimelineModel> model;
};

TEST_F(MonthProjectionTest, RangeReadPreservesDayRowsSelectionAndRefreshDate) {
  const auto date = QDate(2026, 10, 12);
  const auto id = model->addEvent(eventAt(date));
  ASSERT_GT(model->addEventSeries(eventAt(date, QTime(10, 0)), 0,
                                 "FREQ=WEEKLY;BYDAY=MO;COUNT=1", std::nullopt), 0);
  model->loadEventsForDay(date);
  QPersistentModelIndex selection(model->indexForEventId(id));
  ASSERT_EQ(model->events().size(), 2);
  const auto virtualId = model->events().last().id;
  ASSERT_LT(virtualId, 0);
  QPersistentModelIndex virtualSelection(model->indexForEventId(virtualId));
  QSignalSpy resets(model.get(), &QAbstractItemModel::modelReset);
  QSignalSpy loaded(model.get(), &QTimelineModel::eventsLoaded);
  const auto range = model->eventsForRange(QDate(2026, 9, 28), QDate(2026, 11, 1));
  ASSERT_EQ(range.size(), 2);
  EXPECT_EQ(range.first().id, id);
  EXPECT_TRUE(selection.isValid());
  EXPECT_EQ(selection.data(QTimelineModel::IdRole).toLongLong(), id);
  EXPECT_TRUE(virtualSelection.isValid());
  EXPECT_EQ(virtualSelection.data(QTimelineModel::IdRole).toLongLong(), virtualId);
  EXPECT_EQ(resets.count(), 0);
  EXPECT_EQ(loaded.count(), 0);
  ASSERT_EQ(model->events().size(), 2);
  EXPECT_EQ(model->events().first().id, id);
  // Writes reload the selected day: a range read must not change that day.
  model->addEvent(eventAt(date, QTime(11, 0)));
  ASSERT_EQ(model->events().size(), 3);
}

TEST_F(MonthProjectionTest, MovedOccurrenceKeepsOriginalKeyOutsideItsOriginalMonth) {
  const auto seriesId = model->addEventSeries(eventAt(QDate(2026, 9, 28)), 0,
                                              "FREQ=WEEKLY;BYDAY=MO", std::nullopt);
  ASSERT_GT(seriesId, 0);
  model->loadEventsForDay(QDate(2026, 9, 28));
  ASSERT_EQ(model->events().size(), 1);
  auto moved = model->events().first();
  const auto original = moved.original_occurrence_start;
  moved.id = -1;
  moved.start_date = localMs(QDate(2026, 11, 3), QTime(14, 0));
  moved.end_date = *moved.start_date + 3600000;
  moved.is_virtual_occurrence = false;
  const auto id = model->addEvent(moved);
  ASSERT_GT(id, 0);
  EXPECT_TRUE(model->eventsForRange(QDate(2026, 9, 28), QDate(2026, 9, 28)).isEmpty());
  const auto range = model->eventsForRange(QDate(2026, 11, 3), QDate(2026, 11, 3));
  ASSERT_EQ(range.size(), 1);
  EXPECT_EQ(range.first().id, id);
  EXPECT_EQ(range.first().series_id, seriesId);
  EXPECT_EQ(range.first().original_occurrence_start, original);
}

TEST_F(MonthProjectionTest, ExcludedOccurrenceDoesNotReappearInRange) {
  const auto seriesId = model->addEventSeries(eventAt(QDate(2026, 10, 5)), 0,
                                              "FREQ=WEEKLY;BYDAY=MO", std::nullopt);
  ASSERT_TRUE(db->add_event_series_exception(seriesId, localMs(QDate(2026, 10, 12), QTime(9, 0)), "deleted"));
  const auto range = model->eventsForRange(QDate(2026, 10, 5), QDate(2026, 10, 12));
  ASSERT_EQ(range.size(), 1);
  EXPECT_EQ(range.first().original_occurrence_start, localMs(QDate(2026, 10, 5), QTime(9, 0)));
}

TEST_F(MonthProjectionTest, RangeResolvesClientNamesAndRetainsCanceledMaterializedStatus) {
  DuckClient client;
  client.name = "Calendar";
  client.last_name = "Client";
  const auto clientId = db->add_client(client);
  ASSERT_GT(clientId, 0);
  auto event = eventAt(QDate(2026, 10, 5));
  event.is_work_event = true;
  const auto id = db->add_event(event);
  ASSERT_GT(db->add_event_client(id, clientId), 0);
  ASSERT_GT(model->addEventSeries(eventAt(QDate(2026, 10, 12)), 0,
                                 "FREQ=WEEKLY;BYDAY=MO", std::nullopt), 0);
  event.start_date = localMs(QDate(2026, 10, 12), QTime(11, 0));
  event.end_date = *event.start_date + 3600000;
  const auto seriesId = model->addEventSeries(event, clientId, "FREQ=WEEKLY;BYDAY=MO", std::nullopt);
  ASSERT_GT(seriesId, 0);
  auto canceled = event;
  canceled.series_id = seriesId;
  canceled.original_occurrence_start = event.start_date;
  canceled.event_stat_id = 3;
  const auto canceledId = db->add_event(canceled);
  ASSERT_GT(db->add_event_client(canceledId, clientId), 0);
  const auto range = model->eventsForRange(QDate(2026, 10, 5), QDate(2026, 10, 19));
  int clientEvents = 0;
  bool foundCanceled = false;
  for (const auto &item : range) {
    if (item.is_work_event) {
      EXPECT_EQ(item.client_name.value_or(""), "Calendar Client");
      ++clientEvents;
    }
    if (item.id == canceledId) {
      foundCanceled = true;
      EXPECT_EQ(item.event_stat_id, 3);
    }
  }
  EXPECT_EQ(clientEvents, 3);
  EXPECT_TRUE(foundCanceled);
}

TEST_F(MonthProjectionTest, MidnightBoundariesUseHalfOpenOverlap) {
  const auto date = QDate(2026, 10, 12);
  const auto crossing = db->add_event(eventAt(date.addDays(-1), QTime(23, 30), 7200));
  ASSERT_GT(db->add_event(eventAt(date.addDays(-1), QTime(23, 0))), 0); // ends at midnight
  ASSERT_GT(db->add_event(eventAt(date.addDays(1), QTime(0, 0))), 0); // starts at next midnight
  const auto range = model->eventsForRange(date, date);
  ASSERT_EQ(range.size(), 1);
  EXPECT_EQ(range.first().id, crossing);
}

TEST_F(MonthProjectionTest, OvernightRecurrenceOverlapsFirstVisibleDayEvenAfterItsUntil) {
  const auto previous = QDate(2026, 10, 11);
  const auto start = localMs(previous, QTime(23, 30));
  const auto seriesId = model->addEventSeries(eventAt(previous, QTime(23, 30), 7200), 0,
                                              "FREQ=DAILY", start);
  ASSERT_GT(seriesId, 0);
  ASSERT_EQ(db->get_event_series(seriesId)->duration, 7200);
  ASSERT_EQ(db->get_event_series(seriesId)->recurrence_until, start);
  const auto range = model->eventsForRange(previous.addDays(1), previous.addDays(1));
  ASSERT_EQ(range.size(), 1);
  EXPECT_EQ(range.first().series_id, seriesId);
  EXPECT_EQ(range.first().original_occurrence_start, start);
  // Preserve the existing daily projection's start-in-day semantics.
  model->loadEventsForDay(previous.addDays(1));
  EXPECT_TRUE(model->events().isEmpty());
}

TEST_F(MonthProjectionTest, InvalidReversedAndEmptyRangesReturnNoEvents) {
  EXPECT_TRUE(model->eventsForRange({}, QDate(2026, 10, 1)).isEmpty());
  EXPECT_TRUE(model->eventsForRange(QDate(2026, 10, 2), QDate(2026, 10, 1)).isEmpty());
  EXPECT_TRUE(model->eventsForRange(QDate(2026, 10, 1), QDate(2026, 10, 31)).isEmpty());
}

TEST_F(MonthProjectionTest, LocalDstDayIncludesBothRepeatedHoursAndPinnedSeriesTime) {
  ASSERT_EQ(QTimeZone::systemTimeZoneId(), "Europe/Berlin");
  const QDate date(2026, 10, 25);
  EXPECT_EQ(localMs(date.addDays(1)) - localMs(date), 25 * 3600000LL);
  // Both 02:30 instants of the repeated hour belong to the same local day.
  auto first = eventAt(date);
  first.start_date = QDateTime(QDate(2026, 10, 25), QTime(0, 30), QTimeZone::UTC).toMSecsSinceEpoch();
  first.end_date = *first.start_date + 1800000;
  ASSERT_GT(db->add_event(first), 0);
  auto second = first;
  second.start_date = *first.start_date + 3600000;
  second.end_date = *second.start_date + 1800000;
  ASSERT_GT(db->add_event(second), 0);
  pcm::meeting::SeriesScheduleCommitter committer(*db);
  model->setScheduleCommitter(&committer);
  ASSERT_GT(model->addEventSeries(eventAt(QDate(2026, 10, 18), QTime(18, 0)), 0,
                                 "FREQ=WEEKLY;BYDAY=SU", std::nullopt, "Europe/Berlin"), 0);
  const auto range = model->eventsForRange(date, date);
  ASSERT_EQ(range.size(), 3);
  EXPECT_EQ(range.last().start_date, localMs(date, QTime(18, 0)));
  EXPECT_EQ(*range.last().start_date - localMs(QDate(2026, 10, 18), QTime(18, 0)), 169 * 3600000LL);
}

TEST(MonthCalendarTest, LocaleOriginAndMonthBoundariesHaveThirtyFiveOrFortyTwoCells) {
  const auto previousLocale = QLocale();
  QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
  MonthCalendarWidget widget;
  widget.setMonth(QDate(2026, 10, 17));
  EXPECT_EQ(widget.firstVisibleDate(), QDate(2026, 9, 28));
  EXPECT_EQ(widget.lastVisibleDate(), QDate(2026, 11, 1));
  EXPECT_EQ(widget.findChildren<QToolButton *>("monthDate").size(), 35);
  widget.setMonth(QDate(2026, 3, 1));
  EXPECT_EQ(widget.firstVisibleDate(), QDate(2026, 2, 23));
  EXPECT_EQ(widget.lastVisibleDate(), QDate(2026, 4, 5));
  EXPECT_EQ(widget.findChildren<QToolButton *>("monthDate").size(), 42);
  widget.setMonth(QDate(2027, 1, 1));
  EXPECT_EQ(widget.firstVisibleDate(), QDate(2026, 12, 28));
  widget.setMonth(QDate(2021, 2, 1));
  EXPECT_EQ(widget.firstVisibleDate().daysTo(widget.lastVisibleDate()) + 1, 35);
  QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
  widget.setMonth(QDate(2026, 10, 1));
  EXPECT_EQ(widget.firstVisibleDate(), QDate(2026, 9, 27));
  EXPECT_EQ(widget.lastVisibleDate(), QDate(2026, 10, 31));
  QLocale::setDefault(previousLocale);
}

TEST(MonthCalendarTest, AdjacentDateAndCopiedOccurrenceRemainSelectable) {
  MonthCalendarWidget widget;
  widget.resize(900, 600);
  widget.setMonth(QDate(2026, 10, 1));
  auto event = eventAt(widget.firstVisibleDate());
  event.id = -99;
  event.series_id = 7;
  event.original_occurrence_start = event.start_date;
  event.is_virtual_occurrence = true;
  QVector<DuckEvent> events{event};
  widget.setEvents(events);
  events.first().series_id = 99; // The widget owns the supplied copy.
  widget.show();
  QApplication::processEvents();
  QSignalSpy dates(&widget, &MonthCalendarWidget::dateSelected);
  QSignalSpy selected(&widget, &MonthCalendarWidget::eventSelected);
  const auto buttons = widget.findChildren<QPushButton *>("monthEvent");
  ASSERT_EQ(buttons.size(), 1);
  ASSERT_TRUE(buttons.first()->isVisible());
  QTest::mouseClick(buttons.first(), Qt::LeftButton);
  ASSERT_EQ(selected.count(), 1);
  const auto copy = selected.first().first().value<DuckEvent>();
  EXPECT_EQ(copy.series_id, 7);
  EXPECT_EQ(copy.original_occurrence_start, event.original_occurrence_start);
  EXPECT_TRUE(copy.is_virtual_occurrence);
  const auto dateButtons = widget.findChildren<QToolButton *>("monthDate");
  QTest::mouseClick(dateButtons.first(), Qt::LeftButton);
  ASSERT_EQ(dates.count(), 1);
  EXPECT_EQ(dates.first().first().toDate(), widget.firstVisibleDate());
  widget.setSelectedDate(widget.firstVisibleDate());
  EXPECT_TRUE(dateButtons.first()->isChecked());
}

TEST(MonthCalendarTest, DenseDayOverflowSelectsEveryHiddenEventAtSmallHeight) {
  MonthCalendarWidget widget;
  widget.resize(600, 330);
  widget.setMonth(QDate(2026, 10, 1));
  QVector<DuckEvent> events;
  const auto date = QDate(2026, 10, 12);
  for (int i = 0; i < 12; ++i) {
    auto event = eventAt(date, QTime(8, 0).addSecs(i * 1800));
    event.id = 100 + i;
    event.name = std::string(180, 'W');
    events.append(event);
  }
  widget.setEvents(events);
  widget.show();
  QApplication::processEvents();
  QToolButton *overflow = nullptr;
  for (auto *button : widget.findChildren<QToolButton *>("monthOverflow")) {
    if (button->property("calendarDate").toDate() == date && button->isVisible()) {
      overflow = button;
    }
  }
  ASSERT_NE(overflow, nullptr);
  QSignalSpy selected(&widget, &MonthCalendarWidget::eventSelected);
  QTest::mouseClick(overflow, Qt::LeftButton);
  QApplication::processEvents();
  const auto menus = widget.findChildren<QMenu *>();
  ASSERT_FALSE(menus.isEmpty());
  auto *menu = menus.last();
  int visibleCount = 0;
  for (auto *button : widget.findChildren<QPushButton *>("monthEvent")) {
    if (button->isVisible()) {
      ++visibleCount;
      EXPECT_FALSE(button->toolTip().isEmpty());
      EXPECT_LT(button->text().size(), 180);
    }
  }
  ASSERT_EQ(menu->actions().size() + visibleCount, 12);
  for (auto *action : menu->actions()) {
    action->trigger();
  }
  EXPECT_EQ(selected.count(), menu->actions().size());
  EXPECT_EQ(selected.last().first().value<DuckEvent>().id, 111);
  menu->close();
}

TEST(MonthCalendarTest, OvernightEventAppearsInBothDaysAndEndsAtExclusiveMidnight) {
  MonthCalendarWidget widget;
  widget.resize(900, 600);
  widget.setMonth(QDate(2026, 10, 1));
  auto overnight = eventAt(QDate(2026, 10, 12), QTime(23, 30), 7200);
  overnight.id = 10;
  auto ending = eventAt(QDate(2026, 10, 14), QTime(23, 0));
  ending.id = 11;
  widget.setEvents({overnight, ending});
  EXPECT_EQ(widget.findChildren<QPushButton *>("monthEvent").size(), 3);
}

TEST(MonthCalendarTest, SelectedDayBadgeShowsItsNumberAgainstTheFilledCircle) {
  MonthCalendarWidget widget;
  widget.resize(900, 700);
  widget.setMonth(QDate(2026, 10, 1));
  widget.setSelectedDate(QDate(2026, 10, 8));
  widget.show();
  QApplication::processEvents();
  QToolButton *selected = nullptr;
  for (auto *button : widget.findChildren<QToolButton *>("monthDate")) {
    if (button->property("calendarDate").toDate() == QDate(2026, 10, 8)) selected = button;
  }
  ASSERT_NE(selected, nullptr);
  EXPECT_EQ(selected->text(), "8");
  EXPECT_TRUE(selected->isChecked());
  const auto image = selected->grab().toImage().convertToFormat(QImage::Format_RGB32);
  const auto fill = selected->palette().color(QPalette::Highlight);
  int fillPixels = 0;
  int numberPixels = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const QColor c = image.pixelColor(x, y);
      if (c == fill) ++fillPixels;
      // glyph pixels: far from the circle colour and much lighter than the cell
      else if (std::abs(c.lightness() - fill.lightness()) > 60 && c.lightness() > 180) ++numberPixels;
    }
  }
  EXPECT_GT(fillPixels, 200);   // a filled badge
  EXPECT_GT(numberPixels, 15);  // with a visible number on it
}

TEST(MonthCalendarTest, RowsShareTheWholeHeightEvenInATallWindow) {
  MonthCalendarWidget widget;
  widget.setMonth(QDate(2026, 10, 1));
  widget.resize(1000, 1500);
  widget.show();
  QApplication::processEvents();
  const auto dates = widget.findChildren<QToolButton *>("monthDate");
  ASSERT_EQ(dates.size(), 35);
  int bottom = 0;
  int minHeight = 100000;
  int maxHeight = 0;
  for (auto *button : dates) {
    auto *cell = button->parentWidget();
    bottom = std::max(bottom, cell->geometry().bottom() + 1);
    minHeight = std::min(minHeight, cell->height());
    maxHeight = std::max(maxHeight, cell->height());
  }
  EXPECT_GE(minHeight, 96);
  EXPECT_LE(maxHeight - minHeight, 1);        // equal rows
  EXPECT_EQ(bottom, widget.height());         // the last row ends at the widget bottom
  EXPECT_EQ(widget.height(), 1500);
}


QPushButton *chipNamed(MonthCalendarWidget &widget, const QString &name) {
  for (auto *button : widget.findChildren<QPushButton *>("monthEvent")) {
    if (button->accessibleName().contains(name)) return button;
  }
  return nullptr;
}

MonthCalendarWidget *colorWidget(std::unique_ptr<MonthCalendarWidget> &holder) {
  holder = std::make_unique<MonthCalendarWidget>();
  holder->resize(1100, 700);
  holder->setMonth(QDate(2026, 10, 1));
  auto work = eventAt(QDate(2026, 10, 12), QTime(9, 0));
  work.name = "WorkChip";
  work.is_work_event = true;
  auto personal = eventAt(QDate(2026, 10, 13), QTime(9, 0));
  personal.name = "PersonalChip";
  personal.is_work_event = false;
  holder->setEvents({work, personal});
  holder->show();
  QApplication::processEvents();
  return holder.get();
}

TEST(MonthCalendarTest, ChipsUseTheSameWorkAndPersonalColorsAsTheDayTimeline) {
  pcm::app_settings::setWorkEventColor(QColor(20, 160, 60));
  pcm::app_settings::setPersonalEventColor(QColor(240, 140, 20));
  std::unique_ptr<MonthCalendarWidget> holder;
  auto *widget = colorWidget(holder);
  auto *work = chipNamed(*widget, "WorkChip");
  auto *personal = chipNamed(*widget, "PersonalChip");
  ASSERT_NE(work, nullptr);
  ASSERT_NE(personal, nullptr);
  EXPECT_EQ(work->property("chipFill").value<QColor>(), QColor(20, 160, 60));
  EXPECT_EQ(work->property("chipBorder").value<QColor>(), QColor(20, 160, 60).darker(165));
  EXPECT_EQ(personal->property("chipFill").value<QColor>(), QColor(240, 140, 20));
  EXPECT_EQ(personal->property("chipBorder").value<QColor>(), QColor(240, 140, 20).darker(165));
}

TEST(MonthCalendarTest, ChipColorsFollowSettingsAfterRefreshAppearance) {
  pcm::app_settings::setWorkEventColor(QColor(20, 160, 60));
  pcm::app_settings::setPersonalEventColor(QColor(240, 140, 20));
  std::unique_ptr<MonthCalendarWidget> holder;
  auto *widget = colorWidget(holder);
  pcm::app_settings::setWorkEventColor(QColor(200, 30, 30));
  pcm::app_settings::setPersonalEventColor(QColor(30, 30, 200));
  widget->refreshAppearance();
  EXPECT_EQ(chipNamed(*widget, "WorkChip")->property("chipFill").value<QColor>(),
            QColor(200, 30, 30));
  EXPECT_EQ(chipNamed(*widget, "PersonalChip")->property("chipFill").value<QColor>(),
            QColor(30, 30, 200));
}

TEST(MonthCalendarTest, ChipTextContrastsWithVeryLightAndVeryDarkColors) {
  for (const QColor color : {QColor(255, 250, 200), QColor(10, 10, 40)}) {
    pcm::app_settings::setWorkEventColor(color);
    pcm::app_settings::setPersonalEventColor(color);
    std::unique_ptr<MonthCalendarWidget> holder;
    auto *widget = colorWidget(holder);
    auto *chip = chipNamed(*widget, "WorkChip");
    ASSERT_NE(chip, nullptr);
    const auto fill = chip->property("chipFill").value<QColor>();
    const auto text = chip->property("chipText").value<QColor>();
    EXPECT_GE(std::abs(fill.lightness() - text.lightness()), 120) << color.name().toStdString();
  }
}

} // namespace

int main(int argc, char **argv) {
  // Set before constructing Qt; restoration is unnecessary in this dedicated process.
  qputenv("TZ", "Europe/Berlin");
  tzset();
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("SessioMonthCalendarTests");
  QCoreApplication::setApplicationName("MonthCalendarTests");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
