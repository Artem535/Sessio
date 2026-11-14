#include "service/schedule_service.h"
#include "auth/static_token_authorizer.h"
#include "db/migrations.h"
#include "service/schedule_wire.h"
#include <gtest/gtest.h>

using namespace pcm::tokenbackend;
namespace {
constexpr auto uid = "45f9c587-94c8-4d45-9f21-422a8df32590";
const std::string payload = R"({"schema_version":1,"revision":1,"base_revision":0,"timezone":"Europe/Moscow","dtstart_local":"2026-10-06T18:00:00","duration_seconds":3600,"rrule":"FREQ=WEEKLY;INTERVAL=1;BYDAY=TU","until_utc":null,"active":true,"join_enabled":true,"overrides":[],"exceptions":[]})";
class ScheduleServiceTest : public ::testing::Test {
protected:
  SqliteConnection conn{":memory:"};
  AccountsRepository accounts{conn};
  StaticTokenAuthorizer authorizer{accounts};
  ScheduleRepository repo{conn};
  ScheduleService service{authorizer, repo};
  std::string credential;
  void SetUp() override { runMigrations(conn); credential = "Bearer " + accounts.createAccount(); }
};
TEST_F(ScheduleServiceTest, AuthorizedSnapshotRoundTripsAndCapabilitiesRequireAuthentication) {
  EXPECT_FALSE(service.supportsScheduleSeries(""));
  EXPECT_TRUE(service.supportsScheduleSeries(credential));
  EXPECT_EQ(service.put("", uid, payload).error, ScheduleError::Unauthorized);
  auto put = service.put(credential, uid, payload); ASSERT_TRUE(put.ok());
  auto get = service.get(credential, uid); ASSERT_TRUE(get.ok());
  EXPECT_EQ(get.value->contentHash, put.value->contentHash);
  EXPECT_EQ(get.value->snapshot.dtstartLocal, "2026-10-06T18:00:00");
  auto other = "Bearer " + accounts.createAccount();
  EXPECT_EQ(service.get(other, uid).error, ScheduleError::NotFound);
  EXPECT_EQ(service.put(other, uid, payload).error, ScheduleError::NotFound);
}
TEST_F(ScheduleServiceTest, StrictSchemaAndBoundedRawPayloadCannotMutateConfirmedRevision) {
  ASSERT_TRUE(service.put(credential, uid, payload).ok());
  auto replace = [&](std::string from, std::string to) {
    auto p = payload; auto pos = p.find(from); EXPECT_NE(pos, std::string::npos);
    p.replace(pos, from.size(), to); return p;
  };
  const std::vector<std::string> invalid = {
    replace("\"schema_version\":1", "\"schema_version\":2"),
    replace("\"join_enabled\":true,", ""),
    replace("\"active\":true", "\"active\":1"),
    replace("\"revision\":1", "\"revision\":\"1\""),
    replace("\"revision\":1", "\"revision\":1.0"),
    replace("\"revision\":1", "\"revision\":01"),
    replace("\"revision\":1", "\"revision\":9223372036854775808"),
    replace("\"timezone\":\"Europe/Moscow\"", "\"timezone\":\"Unknown/Zone\""),
    replace("\"rrule\":\"FREQ=WEEKLY;INTERVAL=1;BYDAY=TU\"", "\"rrule\":\"FREQ=DAILY;COUNT=2\""),
    replace("\"until_utc\":null", "\"until_utc\":\"2026-10-06T15:00:00.000Z\""),
    replace("\"until_utc\":null", "\"until_utc\":\"2026-02-30T15:00:00Z\""),
    replace("\"until_utc\":null", "\"until_utc\":\"2026-10-06T15:00:00+00:00\""),
    replace("\"overrides\":[]", "\"overrides\":[{\"original_start_utc\":\"2026-10-13T15:00:00Z\",\"start_utc\":\"2026-10-14T15:00:00Z\",\"end_utc\":\"2026-10-14T16:00:00Z\"}]"),
    replace("\"exceptions\":[]", "\"exceptions\":[true]"),
    replace("\"exceptions\":[]", "\"exceptions\":[\"2026-10-13T15:00:00Z\",\"2026-10-13T15:00:00Z\"]"),
    replace("\"active\":true", "\"active\":true,\"active\":false"),
    replace("\"active\":true", "\"active\":true,\"client_name\":\"secret\""),
    payload + " {}", "{}", "null", "[]", "{", replace("\"exceptions\":[]", "\"exceptions\":[],")
  };
  for (const auto &bad : invalid) {
    auto result = service.put(credential, uid, bad);
    EXPECT_EQ(result.error, ScheduleError::Invalid) << bad;
    EXPECT_EQ(result.reason.find("secret"), std::string::npos);
  }
  EXPECT_EQ(service.put(credential, uid, std::string(1024 * 1024 + 1, ' ')).error, ScheduleError::TooLarge);
  EXPECT_EQ(service.get(credential, uid).value->snapshot.revision, 1);
  EXPECT_EQ(service.put(credential, "wrong-uid", payload).error, ScheduleError::Invalid);
}
TEST_F(ScheduleServiceTest, SyncRateLimitCountsRetriesPerAccount) {
  for (int i = 0; i < 60; ++i) ASSERT_TRUE(service.put(credential, uid, payload).ok());
  EXPECT_EQ(service.put(credential, uid, payload).error, ScheduleError::TooManyRequests);
  auto other = "Bearer " + accounts.createAccount();
  EXPECT_TRUE(service.put(other, "45f9c587-94c8-4d45-9f21-422a8df32593", payload).ok());
}
TEST_F(ScheduleServiceTest, CollectionAndRuleLimitsRejectWithoutTruncationAndExactRawLimitIsAccepted) {
  auto s = parseScheduleJson(payload);
  s.exceptions.resize(1001, 1791903600000);
  EXPECT_EQ(service.put(credential, uid, scheduleJson(s)).error, ScheduleError::Invalid);
  s.exceptions.clear();
  s.overrides.resize(1001, {1791903600000, 1791990000000, 1791993600000, true});
  EXPECT_EQ(service.put(credential, uid, scheduleJson(s)).error, ScheduleError::Invalid);
  s.overrides.clear(); s.rrule = std::string(513, 'X');
  EXPECT_EQ(service.put(credential, uid, scheduleJson(s)).error, ScheduleError::Invalid);
  s = parseScheduleJson(payload); s.durationSeconds = 86401;
  EXPECT_EQ(service.put(credential, uid, scheduleJson(s)).error, ScheduleError::Invalid);
  auto padded = payload; padded.resize(1024 * 1024, ' ');
  EXPECT_TRUE(service.put(credential, uid, padded).ok());
}
}
