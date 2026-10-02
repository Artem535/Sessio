#include "series_call_status_text.h"

#include <QCoreApplication>
#include <gtest/gtest.h>

using namespace pcm::eventpage;
using namespace pcm::meeting;

namespace {

SeriesCallStatus published(ScheduleSyncState state, qint64 acked = 1, bool unconfirmed = false) {
  SeriesCallStatus status;
  status.published = true;
  status.sync.state = state;
  status.sync.ackedRevision = acked;
  status.sync.unconfirmed = unconfirmed;
  return status;
}

bool anyLineContains(const SeriesStatusView &view, const QString &text) {
  return view.lines.join('\n').contains(text);
}

} // namespace

TEST(SeriesCallStatusTextTest, UnacknowledgedLocalCancelWarnsThatTheServerMayStillAllowEntry) {
  const auto view = describeSeriesCallStatus(published(ScheduleSyncState::WaitingForNetwork, 2, true));
  EXPECT_TRUE(anyLineContains(view, "Waiting for the network"));
  EXPECT_TRUE(anyLineContains(view, "may still allow entry under the previous schedule"));
  EXPECT_TRUE(view.canRetry);
  EXPECT_EQ(view.severity, StatusSeverity::Warning);
}

TEST(SeriesCallStatusTextTest, FirstPublicationHasNoOldScheduleOnTheServerToWarnAbout) {
  const auto view = describeSeriesCallStatus(published(ScheduleSyncState::Queued, 0, true));
  EXPECT_FALSE(anyLineContains(view, "may still allow entry"));
}

TEST(SeriesCallStatusTextTest, SyncedSeriesWithReadyLinkAllowsCopying) {
  auto status = published(ScheduleSyncState::Synced);
  status.invitation.state = InvitationState::Ready;
  const auto view = describeSeriesCallStatus(status);
  EXPECT_TRUE(view.linkReady);
  EXPECT_FALSE(view.canRetry);
  EXPECT_EQ(view.severity, StatusSeverity::Info);
}

TEST(SeriesCallStatusTextTest, ReadyLinkStillOffersANewLinkBehindTheConfirmation) {
  // The secret is not in the database or in backups; after a restore the link
  // can not be copied and the specialist must be able to rotate it.
  auto status = published(ScheduleSyncState::Synced);
  status.invitation.state = InvitationState::Ready;
  const auto view = describeSeriesCallStatus(status);
  EXPECT_TRUE(view.linkReady);
  EXPECT_TRUE(view.canReissue);
}

TEST(SeriesCallStatusTextTest, ConflictOffersTheExplicitOverwriteNotARetry) {
  const auto view = describeSeriesCallStatus(published(ScheduleSyncState::Conflict));
  EXPECT_TRUE(view.canPublishThisDevice);
  EXPECT_FALSE(view.canRetry);
  EXPECT_TRUE(anyLineContains(view, "Nothing was overwritten"));
  EXPECT_EQ(view.severity, StatusSeverity::Error);
}

TEST(SeriesCallStatusTextTest, RejectedAndUnsupportedStatesAreInlineErrorsWithManualRetry) {
  auto rejected = published(ScheduleSyncState::Rejected);
  rejected.sync.detail = "unsupported RRULE field";
  auto view = describeSeriesCallStatus(rejected);
  EXPECT_TRUE(anyLineContains(view, "unsupported RRULE field"));
  EXPECT_TRUE(view.canRetry);

  view = describeSeriesCallStatus(published(ScheduleSyncState::Unsupported, 0, true));
  EXPECT_TRUE(anyLineContains(view, "does not support recurring calls"));
  EXPECT_TRUE(view.canRetry);
  EXPECT_FALSE(view.linkReady);
}

TEST(SeriesCallStatusTextTest, LostLinkOffersReissueAndFailedLinkOffersRetry) {
  auto status = published(ScheduleSyncState::Synced);
  status.invitation.state = InvitationState::NeedsReissue;
  auto view = describeSeriesCallStatus(status);
  EXPECT_TRUE(view.canReissue);
  EXPECT_FALSE(view.linkReady);

  status.invitation.state = InvitationState::Failed;
  status.invitation.detail = "secret_store_unavailable";
  view = describeSeriesCallStatus(status);
  EXPECT_TRUE(view.canRetry);
  EXPECT_TRUE(anyLineContains(view, "keychain"));
}

TEST(SeriesCallStatusTextTest, FailedMigrationStatesThatTheOldLinkStaysValid) {
  SeriesCallStatus status;
  status.migration = {MigrationState::Failed, "schedule_rejected"};
  const auto view = describeSeriesCallStatus(status);
  EXPECT_TRUE(anyLineContains(view, "previous link stays valid"));
  EXPECT_TRUE(view.canRetry);
  EXPECT_EQ(view.severity, StatusSeverity::Error);
}

TEST(SeriesCallStatusTextTest, UnpublishedSeriesWithoutMigrationShowsNothing) {
  EXPECT_TRUE(describeSeriesCallStatus(SeriesCallStatus{}).lines.isEmpty());
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
