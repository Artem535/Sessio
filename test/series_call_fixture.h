#pragma once

#include "series_call_service.h"

#include "config.h"
#include "database.h"
#include "fake_series_invitation_store.h"
#include "qtimeline_model.h"
#include "series_backend_model.h"
#include "series_join_target.h"
#include "token_backend_client.h"

#include <Poco/File.h>
#include <Poco/Path.h>
#include <QElapsedTimer>
#include <QTest>
#include <QTimeZone>
#include <gtest/gtest.h>

#include <functional>
#include <memory>

// The whole recurring-call stack on a real local database against the
// behavioural model of the token-backend: committer, sync, invitation service,
// facade and timeline model. Shared by the service and the UI tests.
namespace series_call_test {

using namespace pcm::meeting;
using pcm::database::Database;
using pcm::tokenclient::TokenBackendClient;

constexpr int64_t kHour = 3'600'000;
constexpr int64_t kDay = 24 * kHour;
constexpr int64_t kFirst = 1792512000000; // 2026-10-20T16:00:00Z, Tuesday 18:00 Europe/Berlin
constexpr auto kRule = "FREQ=WEEKLY;INTERVAL=1;BYDAY=TU";

class SeriesCallFixture : public ::testing::Test {
protected:
  void SetUp() override {
    mName = std::string("tmp_series_call_") +
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
    mConf = pcm::config::Config{
        .db_conf = pcm::config::DatabaseConfig{
            .db_pth = Poco::Path(Poco::Path::current()).append(mName)}};
    if (Poco::File dir(mConf.db_conf().db_pth.toString()); dir.exists()) {
      dir.remove(true);
    }
    mDb = std::make_shared<Database>(mConf);
    mBackend = std::make_unique<FakeScheduleBackend>();
    mBackend->setHandler([this](const FakeScheduleBackend::Request &r) { return mModel.handle(r); });
    mClient = std::make_unique<TokenBackendClient>(mBackend->baseUrl());
    mStore = std::make_unique<FakeSeriesInvitationStore>();
    const auto reader = [](const ScheduleSync::CredentialCallback &done) { done(true, "cred"); };
    mSync = std::make_unique<ScheduleSync>(*mDb, *mClient, reader);
    mSync->setBackoffPolicy([](int) { return 30; });
    mInvitations =
        std::make_unique<SeriesInvitationService>(*mDb, *mSync, *mClient, reader, *mStore);
    mInvitations->setBackoffPolicy([](int) { return 30; });
    mCommitter = std::make_unique<SeriesScheduleCommitter>(*mDb);
    mCommitter->setSync(mSync.get());
    mCoordinator = std::make_unique<MeetingCoordinator>(mBackend->baseUrl(), "cred");
    mService = std::make_unique<SeriesCallService>(*mDb, *mSync, *mInvitations, *mCommitter,
                                                   mCoordinator.get());
    mTimeline = std::make_unique<QTimelineModel>(mDb, mCoordinator.get());
    mTimeline->setScheduleCommitter(mCommitter.get());
  }
  void TearDown() override {
    mTimeline.reset();
    mService.reset();
    mCoordinator.reset();
    mCommitter.reset();
    mInvitations.reset();
    mSync.reset();
    mClient.reset();
    mBackend.reset();
    mDb.reset();
    if (Poco::File dir(mConf.db_conf().db_pth.toString()); dir.exists()) {
      dir.remove(true);
    }
  }

  static DuckEvent liveKitEvent() {
    DuckEvent event;
    event.name = std::string{"Private title"};
    event.event_stat_id = 1;
    event.payment_stat_id = 1;
    event.start_date = kFirst;
    event.end_date = kFirst + kHour;
    event.duration = 3600;
    event.is_online = true;
    event.provider_kind = "LiveKit";
    return event;
  }

  // A series as the previous release created it: one shared LiveKit meeting.
  int64_t createLegacyLiveKitSeries() {
    auto event = liveKitEvent();
    event.meeting_ref = "ref-old";
    event.meeting_url = "https://x/old";
    event.invitation_state = "https://x/old|123456";
    return mTimeline->addEventSeries(event, 0, kRule, std::nullopt);
  }

  std::optional<DuckEvent> occurrenceAt(int64_t startMs) {
    mTimeline->loadEventsForDay(
        QDateTime::fromMSecsSinceEpoch(startMs, QTimeZone::systemTimeZone()).date());
    for (const auto &event : mTimeline->events()) {
      if (event.start_date == startMs) {
        return event;
      }
    }
    return std::nullopt;
  }

  // The legacy calendar follows the machine's wall clock, so tests of legacy
  // series look a day up instead of assuming an exact instant.
  std::optional<DuckEvent> firstEventOnDayOf(int64_t startMs) {
    mTimeline->loadEventsForDay(
        QDateTime::fromMSecsSinceEpoch(startMs, QTimeZone::systemTimeZone()).date());
    if (mTimeline->events().isEmpty()) {
      return std::nullopt;
    }
    return mTimeline->events().first();
  }

  static bool waitFor(const std::function<bool()> &condition, int timeoutMs = 4000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
      QTest::qWait(10);
    }
    return condition();
  }

  std::string mName;
  pcm::config::Config mConf;
  std::shared_ptr<Database> mDb;
  std::unique_ptr<FakeScheduleBackend> mBackend;
  std::unique_ptr<TokenBackendClient> mClient;
  std::unique_ptr<FakeSeriesInvitationStore> mStore;
  std::unique_ptr<ScheduleSync> mSync;
  std::unique_ptr<SeriesInvitationService> mInvitations;
  std::unique_ptr<SeriesScheduleCommitter> mCommitter;
  std::unique_ptr<MeetingCoordinator> mCoordinator;
  std::unique_ptr<SeriesCallService> mService;
  std::unique_ptr<QTimelineModel> mTimeline;
  SeriesBackendModel mModel;
};


} // namespace series_call_test
