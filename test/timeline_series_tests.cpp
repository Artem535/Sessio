#include "qtimeline_model.h"

#include "config.h"
#include "database.h"
#include "fake_schedule_backend.h"
#include "meeting_coordinator.h"
#include "schedule_snapshot.h"
#include "series_schedule_committer.h"

#include <Poco/File.h>
#include <Poco/Path.h>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>
#include <gtest/gtest.h>

#include <memory>

using namespace pcm::meeting;

namespace {

constexpr int64_t kHour = 3'600'000;
constexpr int64_t kDay = 24 * kHour;
constexpr int64_t kFirst = 1792512000000; // 2026-10-20T16:00:00Z, a Tuesday 18:00 in Berlin (CEST)
constexpr auto kRule = "FREQ=WEEKLY;INTERVAL=1;BYDAY=TU";

class TimelineSeriesTest : public ::testing::Test {
protected:
  void SetUp() override {
    mName = std::string("tmp_timeline_series_") +
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
    mConf = pcm::config::Config{
        .db_conf = pcm::config::DatabaseConfig{
            .db_pth = Poco::Path(Poco::Path::current()).append(mName)}};
    if (Poco::File dir(mConf.db_conf().db_pth.toString()); dir.exists()) {
      dir.remove(true);
    }
    mDb = std::make_shared<pcm::database::Database>(mConf);
    mCommitter = std::make_unique<SeriesScheduleCommitter>(*mDb);
    mModel = std::make_unique<QTimelineModel>(mDb, nullptr);
    mModel->setScheduleCommitter(mCommitter.get());
  }
  void TearDown() override {
    mModel.reset();
    mCommitter.reset();
    mDb.reset();
    if (Poco::File dir(mConf.db_conf().db_pth.toString()); dir.exists()) {
      dir.remove(true);
    }
  }

  static DuckEvent liveKitEvent(int64_t start = kFirst) {
    DuckEvent event;
    event.name = std::string{"Private title"};
    event.event_stat_id = 1;
    event.payment_stat_id = 1;
    event.start_date = start;
    event.end_date = start + kHour;
    event.duration = 3600;
    event.is_online = true;
    event.provider_kind = "LiveKit"; // published by the series: no meeting reference
    return event;
  }

  int64_t createPublished(const char *tz = "Europe/Berlin") {
    return mModel->addEventSeries(liveKitEvent(), 0, kRule, std::nullopt, tz);
  }

  std::optional<DuckEvent> virtualOn(int64_t startMs) {
    const auto date = QDateTime::fromMSecsSinceEpoch(startMs, QTimeZone::systemTimeZone()).date();
    mModel->loadEventsForDay(date);
    for (const auto &event : mModel->events()) {
      if (event.start_date == startMs) {
        return event;
      }
    }
    return std::nullopt;
  }

  int64_t desiredRevision(int64_t seriesId) {
    return mDb->get_schedule_identity(seriesId)->desired_revision;
  }

  std::string mName;
  pcm::config::Config mConf;
  std::shared_ptr<pcm::database::Database> mDb;
  std::unique_ptr<SeriesScheduleCommitter> mCommitter;
  std::unique_ptr<QTimelineModel> mModel;
};

} // namespace

TEST_F(TimelineSeriesTest, NewRecurringLiveKitEventCreatesOnePublishedSeriesAndNoMeeting) {
  const auto seriesId = createPublished();

  ASSERT_GT(seriesId, 0);
  const auto identity = mDb->get_schedule_identity(seriesId);
  ASSERT_TRUE(identity.has_value());
  EXPECT_EQ(identity->timezone, "Europe/Berlin"); // captured at creation, never guessed later
  EXPECT_EQ(identity->series_uid.size(), 36u);
  const auto outbox = mDb->get_schedule_outbox(identity->series_uid);
  ASSERT_TRUE(outbox.has_value());
  ASSERT_TRUE(outbox->pending_payload.has_value());
  const auto payload = QJsonDocument::fromJson(QByteArray::fromStdString(*outbox->pending_payload)).object();
  EXPECT_EQ(payload["rrule"].toString(), kRule);
  EXPECT_EQ(payload["dtstart_local"].toString(), "2026-10-20T18:00:00");
  EXPECT_EQ(payload["overrides"].toArray().size(), 0); // one series, not N copies
  EXPECT_FALSE(outbox->pending_payload->find("Private") != std::string::npos);

  const auto stored = mDb->get_event_series(seriesId);
  ASSERT_TRUE(stored);
  EXPECT_FALSE(stored->meeting_ref.has_value()); // no scheduled single meeting copied to occurrences
  EXPECT_EQ(stored->provider_kind.value_or(""), "LiveKit");
  // The next occurrence is the same series: same identity, nothing materialized.
  const auto next = virtualOn(kFirst + 7 * kDay + kHour); // 2026-10-27 after the DST change
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next->series_id, seriesId);
  EXPECT_EQ(next->original_occurrence_start, next->start_date);
  EXPECT_EQ(mDb->get_materialized_occurrence_starts_for_series(seriesId).size(), 0u);
}

TEST_F(TimelineSeriesTest, ScheduleThatCannotBePublishedRollsBackAndSurfacesTheFailure) {
  QSignalSpy failed(mModel.get(), &QTimelineModel::scheduleCommitFailed);

  const auto badRule =
      mModel->addEventSeries(liveKitEvent(), 0, "FREQ=WEEKLY;INTERVAL=1;BYSETPOS=2", std::nullopt,
                             "Europe/Berlin");
  EXPECT_EQ(badRule, 0);
  ASSERT_EQ(failed.count(), 1);
  EXPECT_EQ(failed.at(0).at(0).toString(), "invalid_schedule");
  EXPECT_FALSE(mModel->lastScheduleError().isEmpty());

  const auto badZone = mModel->addEventSeries(liveKitEvent(), 0, kRule, std::nullopt, "Not/AZone");
  EXPECT_EQ(badZone, 0);
  EXPECT_EQ(failed.count(), 2);

  // Nothing of either attempt remains locally.
  EXPECT_FALSE(mDb->get_event_series(1));
  EXPECT_TRUE(mDb->list_schedule_series_pending_sync().empty());
}

TEST_F(TimelineSeriesTest, LegacySeriesStaysOutsideTheOutboxUntilMigrated) {
  const auto seriesId = mModel->addEventSeries(liveKitEvent(), 0, kRule, std::nullopt);
  ASSERT_GT(seriesId, 0);
  EXPECT_FALSE(mDb->get_schedule_identity(seriesId).has_value());

  auto edited = liveKitEvent(kFirst + kHour);
  EXPECT_TRUE(mModel->updateEventSeries(edited, seriesId, 0, kRule, std::nullopt));
  EXPECT_TRUE(mModel->deactivateEventSeries(seriesId));
  EXPECT_FALSE(mDb->get_schedule_identity(seriesId).has_value());
  EXPECT_TRUE(mDb->list_schedule_series_pending_sync().empty());
}

TEST_F(TimelineSeriesTest, TitleOnlyEditOfAPublishedSeriesDoesNotEnqueueANewRevision) {
  const auto seriesId = createPublished();
  ASSERT_GT(seriesId, 0);
  const auto before = desiredRevision(seriesId);

  auto renamed = liveKitEvent();
  renamed.name = std::string{"New private title"};
  ASSERT_TRUE(mModel->updateEventSeries(renamed, seriesId, 0, kRule, std::nullopt));

  EXPECT_EQ(desiredRevision(seriesId), before);
  EXPECT_EQ(mDb->get_event_series(seriesId)->name.value_or(""), "New private title");
}

TEST_F(TimelineSeriesTest, WholeSeriesTimeChangeKeepsThePinnedTimezoneAndQueuesOneNewRevision) {
  const auto seriesId = createPublished();
  ASSERT_GT(seriesId, 0);
  const auto before = desiredRevision(seriesId);

  // The editor passes the occurrence as displayed (one hour later).
  auto moved = liveKitEvent(kFirst + kHour);
  ASSERT_TRUE(mModel->updateEventSeries(moved, seriesId, 0, kRule, std::nullopt));

  EXPECT_EQ(desiredRevision(seriesId), before + 1);
  const auto identity = mDb->get_schedule_identity(seriesId);
  EXPECT_EQ(identity->timezone, "Europe/Berlin");
  const auto payload = QJsonDocument::fromJson(
      QByteArray::fromStdString(*mDb->get_schedule_outbox(identity->series_uid)->pending_payload)).object();
  EXPECT_EQ(payload["dtstart_local"].toString(), "2026-10-20T19:00:00");
}

TEST_F(TimelineSeriesTest, CancelingOneOccurrenceCommitsAnExceptionWithTheSnapshot) {
  const auto seriesId = createPublished();
  ASSERT_GT(seriesId, 0);
  const auto before = desiredRevision(seriesId);
  const auto second = kFirst + 7 * kDay + kHour; // 2026-10-27T17:00Z
  const auto occurrence = virtualOn(second);
  ASSERT_TRUE(occurrence.has_value());

  mModel->removeEvent(occurrence->id);

  EXPECT_EQ(desiredRevision(seriesId), before + 1);
  const auto identity = mDb->get_schedule_identity(seriesId);
  const auto payload = QJsonDocument::fromJson(
      QByteArray::fromStdString(*mDb->get_schedule_outbox(identity->series_uid)->pending_payload)).object();
  ASSERT_EQ(payload["exceptions"].toArray().size(), 1);
  EXPECT_EQ(payload["exceptions"].toArray().at(0).toString(), "2026-10-27T17:00:00Z");
  EXPECT_FALSE(virtualOn(second).has_value());
}

TEST_F(TimelineSeriesTest, MovingOneOccurrenceKeepsItsOriginalKeyInTheSnapshot) {
  const auto seriesId = createPublished();
  ASSERT_GT(seriesId, 0);
  const auto second = kFirst + 7 * kDay + kHour;
  const auto occurrence = virtualOn(second);
  ASSERT_TRUE(occurrence.has_value());

  auto moved = *occurrence;
  moved.id = -1;
  moved.start_date = second + kDay; // Wednesday
  moved.end_date = *moved.start_date + kHour;
  moved.is_virtual_occurrence = false;
  const auto id = mModel->addEvent(moved, true);
  ASSERT_GT(id, 0);

  const auto identity = mDb->get_schedule_identity(seriesId);
  EXPECT_EQ(identity->desired_revision, 2);
  const auto payload = QJsonDocument::fromJson(
      QByteArray::fromStdString(*mDb->get_schedule_outbox(identity->series_uid)->pending_payload)).object();
  ASSERT_EQ(payload["overrides"].toArray().size(), 1);
  const auto override_ = payload["overrides"].toArray().at(0).toObject();
  EXPECT_EQ(override_["original_start_utc"].toString(), "2026-10-27T17:00:00Z");
  EXPECT_EQ(override_["start_utc"].toString(), "2026-10-28T17:00:00Z");
}

TEST_F(TimelineSeriesTest, CanceledOccurrenceStatusClosesJoiningInTheSnapshot) {
  const auto seriesId = createPublished();
  ASSERT_GT(seriesId, 0);
  const auto second = kFirst + 7 * kDay + kHour;
  auto occurrence = virtualOn(second);
  ASSERT_TRUE(occurrence.has_value());
  occurrence->id = -1;
  occurrence->event_stat_id = 3; // canceled
  occurrence->is_virtual_occurrence = false;
  ASSERT_GT(mModel->addEvent(*occurrence, true), 0);

  const auto identity = mDb->get_schedule_identity(seriesId);
  const auto payload = QJsonDocument::fromJson(
      QByteArray::fromStdString(*mDb->get_schedule_outbox(identity->series_uid)->pending_payload)).object();
  ASSERT_EQ(payload["overrides"].toArray().size(), 1);
  EXPECT_FALSE(payload["overrides"].toArray().at(0).toObject()["join_enabled"].toBool());
}

TEST_F(TimelineSeriesTest, SplittingAPublishedSeriesIsBlockedAndChangesNothing) {
  const auto seriesId = createPublished();
  ASSERT_GT(seriesId, 0);
  const auto before = desiredRevision(seriesId);
  QSignalSpy failed(mModel.get(), &QTimelineModel::scheduleCommitFailed);

  EXPECT_FALSE(mModel->removeFutureEventSeriesOccurrences(seriesId, kFirst + 7 * kDay + kHour));

  EXPECT_EQ(desiredRevision(seriesId), before);
  EXPECT_FALSE(mDb->get_event_series(seriesId)->recurrence_until.has_value());
  ASSERT_EQ(failed.count(), 1);
  EXPECT_EQ(failed.at(0).at(0).toString(), "split_unsupported");
  EXPECT_TRUE(mModel->isSeriesSplitBlocked(seriesId));
}

TEST_F(TimelineSeriesTest, DeletingAPublishedSeriesQueuesAnInactiveSnapshot) {
  const auto seriesId = createPublished();
  ASSERT_GT(seriesId, 0);

  ASSERT_TRUE(mModel->deactivateEventSeries(seriesId));

  const auto identity = mDb->get_schedule_identity(seriesId);
  const auto payload = QJsonDocument::fromJson(
      QByteArray::fromStdString(*mDb->get_schedule_outbox(identity->series_uid)->pending_payload)).object();
  EXPECT_FALSE(payload["active"].toBool());
  EXPECT_EQ(identity->desired_revision, 2);
}

TEST_F(TimelineSeriesTest, RemovingAPublishedOccurrenceNeverInvalidatesAMeetingWithoutReference) {
  FakeScheduleBackend backend;
  MeetingCoordinator coordinator(backend.baseUrl(), "cred");
  QTimelineModel model(mDb, &coordinator);
  model.setScheduleCommitter(mCommitter.get());
  const auto seriesId = model.addEventSeries(liveKitEvent(), 0, kRule, std::nullopt, "Europe/Berlin");
  ASSERT_GT(seriesId, 0);
  const auto second = kFirst + 7 * kDay + kHour;
  const auto date = QDateTime::fromMSecsSinceEpoch(second, QTimeZone::systemTimeZone()).date();
  model.loadEventsForDay(date);
  int64_t id = 0;
  for (const auto &event : model.events()) {
    if (event.start_date == second) {
      id = event.id;
    }
  }
  ASSERT_NE(id, 0);

  model.removeEvent(id);
  QTest::qWait(150);

  EXPECT_TRUE(backend.requests().isEmpty()); // no POST /v1/meetings//invalidate
}

TEST_F(TimelineSeriesTest, AddingAnOccurrenceToALegacySeriesDoesNotEnterTheOutbox) {
  const auto seriesId = mModel->addEventSeries(liveKitEvent(), 0, kRule, std::nullopt);
  ASSERT_GT(seriesId, 0);
  // The legacy calendar follows the machine's wall clock, so look the day up
  // instead of assuming an instant.
  mModel->loadEventsForDay(QDateTime::fromMSecsSinceEpoch(kFirst + 7 * kDay, QTimeZone::systemTimeZone()).date());
  ASSERT_FALSE(mModel->events().isEmpty());
  auto occurrence = mModel->events().first();
  occurrence.id = -1;
  occurrence.is_virtual_occurrence = false;
  ASSERT_GT(mModel->addEvent(occurrence, true), 0);
  EXPECT_FALSE(mDb->get_schedule_identity(seriesId).has_value());
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
