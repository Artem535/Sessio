#include "series_call_fixture.h"

#include "config.h"
#include "database.h"
#include "fake_series_invitation_store.h"
#include "qtimeline_model.h"
#include "series_backend_model.h"
#include "series_join_target.h"
#include "token_backend_client.h"

#include <Poco/File.h>
#include <Poco/Path.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTest>
#include <QTimeZone>
#include <gtest/gtest.h>

#include <memory>

using namespace pcm::meeting;
using pcm::database::Database;
using pcm::tokenclient::TokenBackendClient;

using namespace series_call_test;
using SeriesCallTest = SeriesCallFixture;

TEST_F(SeriesCallTest, NewRecurringSeriesPublishesOnceAndEveryOccurrenceSharesOneInvitation) {
  mSync->start();
  const auto seriesId = mTimeline->addEventSeries(liveKitEvent(), 0, kRule, std::nullopt, "Europe/Berlin");
  ASSERT_GT(seriesId, 0);
  mService->ensureInvitation(seriesId);
  ASSERT_TRUE(waitFor([&] { return mService->status(seriesId).invitation.state == InvitationState::Ready; }));

  // One schedule write, one invitation, and no single-meeting creation at all.
  EXPECT_EQ(mModel.putBodies.size(), 1);
  EXPECT_EQ(mModel.invitations, 1);
  for (const auto &entry : mModel.requestLog) {
    EXPECT_FALSE(entry.startsWith("POST /v1/meetings")) << entry.toStdString();
  }

  // Two different dates: same series, each joined by its own original start.
  const auto first = occurrenceAt(kFirst);
  const auto second = occurrenceAt(kFirst + 7 * kDay + kHour); // after the DST change
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  const auto firstTarget = parseSeriesJoinTarget(mService->joinTargetFor(*first).value_or(""));
  const auto secondTarget = parseSeriesJoinTarget(mService->joinTargetFor(*second).value_or(""));
  ASSERT_TRUE(firstTarget.has_value());
  ASSERT_TRUE(secondTarget.has_value());
  EXPECT_EQ(firstTarget->seriesUid, secondTarget->seriesUid);
  EXPECT_EQ(firstTarget->originalStartMs, kFirst);
  EXPECT_EQ(secondTarget->originalStartMs, kFirst + 7 * kDay + kHour);

  // Copying the link from either occurrence yields the same secret.
  QString firstUrl;
  QString secondUrl;
  mService->loadInvitation(seriesId, [&](bool, const QString &url, const QString &) { firstUrl = url; });
  mService->loadInvitation(seriesId, [&](bool, const QString &url, const QString &) { secondUrl = url; });
  EXPECT_FALSE(firstUrl.isEmpty());
  EXPECT_EQ(firstUrl, secondUrl);
}

TEST_F(SeriesCallTest, MovedOccurrenceIsStillJoinedByItsOriginalStart) {
  mSync->start();
  const auto seriesId = mTimeline->addEventSeries(liveKitEvent(), 0, kRule, std::nullopt, "Europe/Berlin");
  ASSERT_GT(seriesId, 0);
  auto occurrence = occurrenceAt(kFirst + 7 * kDay + kHour);
  ASSERT_TRUE(occurrence.has_value());
  const auto original = *occurrence->original_occurrence_start;
  occurrence->id = -1;
  occurrence->start_date = original + kDay;
  occurrence->end_date = *occurrence->start_date + kHour;
  occurrence->is_virtual_occurrence = false;
  ASSERT_GT(mTimeline->addEvent(*occurrence, true), 0);

  const auto moved = occurrenceAt(original + kDay);
  ASSERT_TRUE(moved.has_value());
  const auto target = parseSeriesJoinTarget(mService->joinTargetFor(*moved).value_or(""));
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(target->originalStartMs, original); // not the displayed, moved start
}

TEST_F(SeriesCallTest, SinglePlainAndLegacyEventsHaveNoSeriesJoinTarget) {
  DuckEvent single = liveKitEvent();
  single.meeting_ref = "ref-single";
  EXPECT_FALSE(mService->joinTargetFor(single).has_value());

  const auto legacyId = createLegacyLiveKitSeries();
  ASSERT_GT(legacyId, 0);
  const auto occurrence = firstEventOnDayOf(kFirst);
  ASSERT_TRUE(occurrence.has_value());
  EXPECT_FALSE(mService->joinTargetFor(*occurrence).has_value());
}

TEST_F(SeriesCallTest, OfflineCancellationIsReportedAsNotSyncedUntilTheServerAcknowledges) {
  mSync->start();
  const auto seriesId = mTimeline->addEventSeries(liveKitEvent(), 0, kRule, std::nullopt, "Europe/Berlin");
  ASSERT_GT(seriesId, 0);
  ASSERT_TRUE(waitFor([&] { return mService->status(seriesId).sync.state == ScheduleSyncState::Synced; }));
  EXPECT_FALSE(mService->status(seriesId).sync.unconfirmed);

  mModel.neverRespondToPut = true; // the cancellation cannot reach the server
  mClient->setScheduleRequestTimeoutMs(300);
  const auto occurrence = occurrenceAt(kFirst + 7 * kDay + kHour);
  ASSERT_TRUE(occurrence.has_value());
  mTimeline->removeEvent(occurrence->id);

  const auto status = mService->status(seriesId);
  EXPECT_NE(status.sync.state, ScheduleSyncState::Synced);
  EXPECT_TRUE(status.sync.unconfirmed); // "the server may still allow entry"
  EXPECT_EQ(status.sync.ackedRevision, 1);

  mModel.neverRespondToPut = false;
  ASSERT_TRUE(waitFor([&] { return mService->status(seriesId).sync.state == ScheduleSyncState::Synced; }, 6000));
  EXPECT_EQ(mService->status(seriesId).sync.ackedRevision, 2);
}

TEST_F(SeriesCallTest, MigrationInvalidatesTheOldMeetingOnlyAfterAckAndStoredInvitation) {
  mSync->start();
  const auto seriesId = createLegacyLiveKitSeries();
  ASSERT_GT(seriesId, 0);

  mService->migrateLegacySeries(seriesId, "Europe/Berlin");
  ASSERT_TRUE(waitFor([&] { return mService->status(seriesId).migration.state == MigrationState::Completed; }));

  // Order on the wire: schedule first, then invitation, then the old meeting.
  ASSERT_TRUE(waitFor([&] { return !mModel.invalidatedMeetings.isEmpty(); }));
  const auto log = mModel.requestLog;
  const auto put = log.indexOf(QRegularExpression("PUT /v1/schedule-series/.*"));
  const auto invitation = log.indexOf(QRegularExpression("POST /v1/schedule-series/.*/invitation"));
  const auto invalidate = log.indexOf(QRegularExpression("POST /v1/meetings/ref-old/invalidate"));
  ASSERT_GE(put, 0);
  ASSERT_GT(invitation, put);
  ASSERT_GT(invalidate, invitation);
  EXPECT_EQ(mModel.invalidatedMeetings, QStringList{"ref-old"});
  EXPECT_FALSE(mStore->secrets.isEmpty());

  const auto after = mDb->get_event_series(seriesId);
  EXPECT_FALSE(after->meeting_ref.has_value());
  EXPECT_FALSE(after->invitation_state.has_value());
  EXPECT_TRUE(mService->isSeriesBacked(*after));
  EXPECT_EQ(mDb->get_schedule_identity(seriesId)->timezone, "Europe/Berlin");
}

TEST_F(SeriesCallTest, MigrationFailurePreservesTheOldLocalRelationship) {
  mSync->start();
  const auto seriesId = createLegacyLiveKitSeries();
  ASSERT_GT(seriesId, 0);
  mModel.putStatusOverride = 422; // the server refuses the schedule

  mService->migrateLegacySeries(seriesId, "Europe/Berlin");
  ASSERT_TRUE(waitFor([&] { return mService->status(seriesId).migration.state == MigrationState::Failed; }));

  EXPECT_TRUE(mModel.invalidatedMeetings.isEmpty()); // old meeting still valid
  EXPECT_EQ(mModel.invitations, 0);
  const auto after = mDb->get_event_series(seriesId);
  EXPECT_EQ(after->meeting_ref.value_or(""), "ref-old");
  EXPECT_EQ(after->invitation_state.value_or(""), "https://x/old|123456");
  EXPECT_EQ(after->meeting_url, "https://x/old");
  EXPECT_FALSE(mService->isSeriesBacked(*after));
  EXPECT_FALSE(mService->joinTargetFor(*firstEventOnDayOf(kFirst)).has_value()); // legacy join path

  // The specialist fixes the cause and retries: the migration completes.
  mModel.putStatusOverride = 0;
  mService->retry(seriesId);
  ASSERT_TRUE(waitFor([&] { return mService->status(seriesId).migration.state == MigrationState::Completed; }));
  ASSERT_TRUE(waitFor([&] { return !mModel.invalidatedMeetings.isEmpty(); }));
  EXPECT_EQ(mModel.invalidatedMeetings, QStringList{"ref-old"});
}

TEST_F(SeriesCallTest, MigrationWithAnUnavailableSecretStoreKeepsTheOldMeeting) {
  mSync->start();
  const auto seriesId = createLegacyLiveKitSeries();
  ASSERT_GT(seriesId, 0);
  mStore->available = false;

  mService->migrateLegacySeries(seriesId, "Europe/Berlin");
  ASSERT_TRUE(waitFor([&] { return mService->status(seriesId).migration.state == MigrationState::Failed; }));
  EXPECT_EQ(mService->status(seriesId).migration.detail, "secret_store_unavailable");
  EXPECT_TRUE(mModel.invalidatedMeetings.isEmpty());
  EXPECT_EQ(mDb->get_event_series(seriesId)->meeting_ref.value_or(""), "ref-old");

  mStore->available = true;
  mService->retry(seriesId);
  ASSERT_TRUE(waitFor([&] { return mService->status(seriesId).migration.state == MigrationState::Completed; }));
  EXPECT_EQ(mModel.invitations, 1); // the retry replayed the same request
}

TEST_F(SeriesCallTest, MigrationRejectsATimezoneTheServerWouldNotKnowBeforeChangingAnything) {
  const auto seriesId = createLegacyLiveKitSeries();
  ASSERT_GT(seriesId, 0);

  mService->migrateLegacySeries(seriesId, "Not/AZone");

  EXPECT_EQ(mService->status(seriesId).migration.state, MigrationState::Failed);
  EXPECT_FALSE(mDb->get_schedule_identity(seriesId).has_value());
  EXPECT_EQ(mDb->get_event_series(seriesId)->meeting_ref.value_or(""), "ref-old");
}

TEST_F(SeriesCallTest, OnlyLegacyLiveKitSeriesCanBeMigrated) {
  const auto seriesId = mTimeline->addEventSeries(liveKitEvent(), 0, kRule, std::nullopt, "Europe/Berlin");
  ASSERT_GT(seriesId, 0);
  mService->migrateLegacySeries(seriesId, "Europe/Berlin"); // already published, no legacy meeting
  EXPECT_EQ(mService->status(seriesId).migration.state, MigrationState::Failed);
  EXPECT_EQ(mService->status(seriesId).migration.detail, "not_a_legacy_livekit_series");
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
