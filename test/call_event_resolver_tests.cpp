#include <Poco/File.h>
#include <Poco/Path.h>
#include <gtest/gtest.h>

#include <QDateTime>
#include <QTime>

#include <memory>
#include <string>

#include "call_event_resolver.h"
#include "config.h"
#include "database.h"
#include "recurrence_utils.h"

namespace {

using pcm::calltranscription::CallEventResolver;

class CallEventResolverTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
    dir_ = Poco::Path(Poco::Path::current())
               .append(std::string("tmp_call_resolver_") + info->name())
               .toString();
    removeDir();
    pcm::config::Config conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir_)}};
    db_ = std::make_shared<pcm::database::Database>(conf);

    DuckClient client;
    client.name = std::string{"Eve"};
    client.last_name = std::string{"E"};
    clientId_ = db_->add_client(client);

    startLocal_ = QDateTime(QDate::currentDate().addDays(3), QTime(10, 0, 0));
    DuckEventSeries series;
    series.name = std::string{"Weekly with Eve"};
    series.client_id = clientId_;
    series.is_work_event = true;
    series.event_stat_id = 1;
    series.payment_stat_id = 1;
    series.start_date = startLocal_.toUTC().toMSecsSinceEpoch();
    series.end_date = *series.start_date + 3'600'000;
    series.duration = 3600;
    series.recurrence_rule =
        pcm::recurrence::weeklyRuleForDate(startLocal_.date()).toStdString();
    seriesId_ = db_->add_event_series(series);
  }

  void TearDown() override {
    db_.reset();
    removeDir();
  }

  void removeDir() {
    Poco::File dir(dir_);
    if (dir.exists()) dir.remove(true);
  }

  // Same formula as recurrence_utils.cpp for the first occurrence plus `weeks`.
  int64_t virtualId(int weeks = 0) const {
    return -(seriesId_ * 1'000'000LL +
             static_cast<int64_t>(startLocal_.addDays(7 * weeks).date().toJulianDay()));
  }
  int64_t occurrenceMs(int weeks = 0) const {
    return startLocal_.addDays(7 * weeks).toUTC().toMSecsSinceEpoch();
  }

  CallEventResolver makeResolver(int *calls = nullptr, bool fail = false) {
    return CallEventResolver(db_, [this, calls, fail](const DuckEvent &event) -> int64_t {
      if (calls) ++*calls;
      if (fail) return 0;
      EXPECT_EQ(event.id, -1);
      EXPECT_FALSE(event.is_virtual_occurrence);
      return db_->add_event(event);
    });
  }

  std::string dir_;
  std::shared_ptr<pcm::database::Database> db_;
  int64_t clientId_ = 0;
  int64_t seriesId_ = 0;
  QDateTime startLocal_;
};

}  // namespace

TEST_F(CallEventResolverTest, PositiveIdIsReturnedUnchanged) {
  int calls = 0;
  auto resolver = makeResolver(&calls);
  EXPECT_EQ(resolver.resolve(42), std::optional<int64_t>(42));
  EXPECT_EQ(calls, 0);
}

TEST_F(CallEventResolverTest, VirtualIdMaterialisesOneEventWithSeriesFields) {
  ASSERT_GT(seriesId_, 0);
  auto resolver = makeResolver();
  const auto id = resolver.resolve(virtualId(1));
  ASSERT_TRUE(id.has_value());
  ASSERT_GT(*id, 0);
  const auto event = db_->get_event(*id);
  ASSERT_NE(event, nullptr);
  EXPECT_EQ(event->series_id, std::optional<int64_t>(seriesId_));
  EXPECT_EQ(event->original_occurrence_start, std::optional<int64_t>(occurrenceMs(1)));
  EXPECT_EQ(event->start_date, std::optional<int64_t>(occurrenceMs(1)));
  EXPECT_EQ(event->name.value_or(""), "Weekly with Eve");
  EXPECT_EQ(event->duration, std::optional<int64_t>(3600));
  EXPECT_FALSE(event->is_virtual_occurrence);
}

TEST_F(CallEventResolverTest, ResolveTwiceCreatesSingleEvent) {
  int calls = 0;
  auto resolver = makeResolver(&calls);
  const auto first = resolver.resolve(virtualId(1));
  const auto second = resolver.resolve(virtualId(1));
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first, second);
  EXPECT_EQ(calls, 1);
}

TEST_F(CallEventResolverTest, ClientIsLinkedToMaterialisedEvent) {
  auto resolver = makeResolver();
  const auto id = resolver.resolve(virtualId(2));
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(db_->get_client_by_event(*id).id, clientId_);
}

TEST_F(CallEventResolverTest, AlreadyMaterialisedOccurrenceIsReused) {
  DuckEvent manual;
  manual.name = std::string{"Hand made"};
  manual.start_date = occurrenceMs(1);
  manual.end_date = occurrenceMs(1) + 3'600'000;
  manual.series_id = seriesId_;
  manual.original_occurrence_start = occurrenceMs(1);
  const auto manualId = db_->add_event(manual);
  ASSERT_GT(manualId, 0);

  int calls = 0;
  auto resolver = makeResolver(&calls);
  EXPECT_EQ(resolver.resolve(virtualId(1)), std::optional<int64_t>(manualId));
  EXPECT_EQ(calls, 0);
}

TEST_F(CallEventResolverTest, UnknownSeriesReturnsNullopt) {
  auto resolver = makeResolver();
  EXPECT_FALSE(resolver.resolve(-(987654LL * 1'000'000LL + startLocal_.date().toJulianDay())));
}

TEST_F(CallEventResolverTest, OccurrenceThatNoLongerExistsReturnsNullopt) {
  int calls = 0;
  auto resolver = makeResolver(&calls);
  // A day the weekly rule does not produce.
  const auto offDay = -(seriesId_ * 1'000'000LL +
                        static_cast<int64_t>(startLocal_.date().addDays(8).toJulianDay()));
  EXPECT_FALSE(resolver.resolve(offDay));
  // An occurrence turned into an exception.
  ASSERT_TRUE(db_->add_event_series_exception(seriesId_, occurrenceMs(1)));
  EXPECT_FALSE(resolver.resolve(virtualId(1)));
  EXPECT_EQ(calls, 0);
}

TEST_F(CallEventResolverTest, MaterialiseFailureReturnsNullopt) {
  int calls = 0;
  auto resolver = makeResolver(&calls, true);
  EXPECT_FALSE(resolver.resolve(virtualId(1)));
  EXPECT_EQ(calls, 1);
  EXPECT_FALSE(db_->get_event_by_series_occurrence(seriesId_, occurrenceMs(1)));
}

TEST_F(CallEventResolverTest, VirtualOccurrenceForIdRoundTrip) {
  const auto occurrence = pcm::recurrence::virtualOccurrenceForId(*db_, virtualId(1));
  ASSERT_TRUE(occurrence.has_value());
  EXPECT_EQ(occurrence->id, virtualId(1));
  EXPECT_TRUE(occurrence->is_virtual_occurrence);
  EXPECT_EQ(occurrence->series_id, std::optional<int64_t>(seriesId_));
  EXPECT_EQ(occurrence->start_date, std::optional<int64_t>(occurrenceMs(1)));
  EXPECT_FALSE(pcm::recurrence::virtualOccurrenceForId(*db_, 5).has_value());
}
