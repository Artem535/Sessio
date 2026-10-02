#include "service/series_service.h"
#include "auth/static_token_authorizer.h"
#include "db/migrations.h"
#include "db/series_repository.h"
#include "crypto/random_token.h"
#include <filesystem>
#include <gtest/gtest.h>
#include <sodium.h>
#include <atomic>
#include <future>
#include <set>

using namespace pcm::tokenbackend;
class SeriesServiceTest : public ::testing::Test {
protected:
  SqliteConnection conn{":memory:"};
  AccountsRepository accounts{conn};
  StaticTokenAuthorizer auth{accounts};
  MeetingsRepository meetings{conn};
  ScheduleRepository schedules{conn};
  Config config{};
  std::string bearer;
  AccountId account;
  std::atomic<int64_t> now{1791298800}; // 2026-10-06 15:00 UTC
  std::unique_ptr<SeriesService> service;
  const std::string uid = "11111111-1111-4111-8111-111111111111";
  const std::string key = "22222222-2222-4222-8222-222222222222";
  pcm::schedule::Snapshot snapshot;
  void SetUp() override {
    ASSERT_GE(sodium_init(), 0);
    runMigrations(conn);
    bearer = "Bearer " + accounts.seedAccount();
    account = *auth.authorize(bearer);
    config.liveKitApiKey = "key"; config.liveKitApiSecret = "stable-test-secret"; config.tokenTtlSeconds = 600;
    config.invitationBaseUrl = "https://join.test/j/";
    snapshot.revision = 1; snapshot.baseRevision = 0; snapshot.timezone = "Europe/Moscow";
    snapshot.dtstartLocal = "2026-10-06T18:00:00"; snapshot.durationSeconds = 3600;
    snapshot.rrule = "FREQ=WEEKLY;BYDAY=TU"; snapshot.active = true; snapshot.joinEnabled = true;
    ASSERT_EQ(schedules.put(account, uid, snapshot).status, ScheduleWriteStatus::Applied);
    restart();
  }
  void restart() { service = std::make_unique<SeriesService>(conn, auth, meetings, config, "wss://test", [this] { return now.load(); }); }
  void publish() { snapshot.baseRevision = snapshot.revision++; ASSERT_EQ(schedules.put(account, uid, snapshot).status, ScheduleWriteStatus::Applied); }
  Result<SeriesService::Invitation> invite(const std::string &k = "") {
    return service->invitation(bearer, uid, k.empty() ? key : k, R"({"reissue":false})");
  }
};

TEST_F(SeriesServiceTest, PersistentInvitationSelectsTwoWeeklyRoomsAndSharedPractitionerRoom) {
  auto invitation = invite(); ASSERT_TRUE(invitation.ok());
  auto client = service->clientToken(invitation.value->code, invitation.value->passcode);
  ASSERT_TRUE(client && client->ok());
  auto practitioner = service->specialistToken(bearer, uid, now * 1000);
  ASSERT_TRUE(practitioner.ok()); EXPECT_EQ(client->value->roomName, practitioner.value->roomName);
  now += 7 * 86400;
  auto next = service->clientToken(invitation.value->code, invitation.value->passcode);
  ASSERT_TRUE(next && next->ok()); EXPECT_NE(next->value->roomName, client->value->roomName);
  auto nextPractitioner = service->specialistToken(bearer, uid, now * 1000);
  ASSERT_TRUE(nextPractitioner.ok()); EXPECT_EQ(next->value->roomName, nextPractitioner.value->roomName);
}

TEST_F(SeriesServiceTest, IdempotentRetrySurvivesRestartAndPayloadChangesConflict) {
  auto first = invite(); ASSERT_TRUE(first.ok()); restart();
  auto retry = invite(); ASSERT_TRUE(retry.ok());
  EXPECT_EQ(first.value->code, retry.value->code); EXPECT_EQ(first.value->passcode, retry.value->passcode);
  EXPECT_EQ(service->invitation(bearer, uid, key, R"({"reissue":true})").error, ServiceError::IdempotencyConflict);
  EXPECT_EQ(invite("33333333-3333-4333-8333-333333333333").error, ServiceError::InvitationExists);
}

TEST_F(SeriesServiceTest, ReplayPreservesOriginalUrlAfterConfigurationChange) {
  auto first = invite(); ASSERT_TRUE(first.ok());
  ASSERT_EQ(first.value->invitationUrl, config.invitationBaseUrl + first.value->code);
  config.invitationBaseUrl = "https://changed.test/j/"; restart();
  auto retry = invite(); ASSERT_TRUE(retry.ok());
  EXPECT_EQ(retry.value->invitationUrl, first.value->invitationUrl);
}

TEST_F(SeriesServiceTest, SixthWrongPasscodeRevokesAcrossRestartAndWeeklyWindows) {
  auto invitation = invite(); ASSERT_TRUE(invitation.ok());
  for (int i = 0; i < 5; ++i) {
    auto rejected = service->clientToken(invitation.value->code, "wrong");
    ASSERT_TRUE(rejected); EXPECT_EQ(rejected->error, ServiceError::WrongPasscode);
  }
  now += 7 * 86400; restart();
  auto sixth = service->clientToken(invitation.value->code, "wrong");
  ASSERT_TRUE(sixth); EXPECT_EQ(sixth->error, ServiceError::InvitationRevoked);
  EXPECT_EQ(service->clientToken(invitation.value->code, invitation.value->passcode)->error, ServiceError::InvitationRevoked);
}

TEST_F(SeriesServiceTest, ReissueAndExplicitRevokeRetireOldCodes) {
  auto old = invite(); ASSERT_TRUE(old.ok());
  auto fresh = service->invitation(bearer, uid, "33333333-3333-4333-8333-333333333333", R"({"reissue":true})");
  ASSERT_TRUE(fresh.ok()); EXPECT_EQ(fresh.value->generation, 2);
  EXPECT_EQ(service->clientToken(old.value->code, old.value->passcode)->error, ServiceError::InvitationRevoked);
  ASSERT_TRUE(service->revoke(bearer, uid).ok());
  EXPECT_EQ(service->clientToken(fresh.value->code, fresh.value->passcode)->error, ServiceError::InvitationRevoked);
  EXPECT_TRUE(service->specialistToken(bearer, uid, now * 1000).ok());
}

TEST_F(SeriesServiceTest, CancelMoveInactiveAndAmbiguityDenyMappedMeetings) {
  const auto original = now * 1000;
  ASSERT_TRUE(service->specialistToken(bearer, uid, original).ok());
  SeriesRepository repository(conn);
  auto meeting = meetings.findById(*repository.meetingId(uid, original)); ASSERT_TRUE(meeting);
  snapshot.exceptions = {original}; publish();
  auto canceled = service->mappedToken(*meeting, false); ASSERT_TRUE(canceled);
  EXPECT_EQ(canceled->error, ServiceError::OccurrenceUnavailable);
  snapshot.overrides = {{original, original + 86400000, original + 90000000, true}}; publish();
  EXPECT_EQ(service->mappedToken(*meeting, false)->error, ServiceError::OccurrenceUnavailable);
  now += 86400;
  auto moved = service->mappedToken(*meeting, true); ASSERT_TRUE(moved && moved->ok());
  EXPECT_EQ(moved->value->roomName, meeting->roomName);
  snapshot.overrides.push_back({original + 7 * 86400000, now * 1000, now * 1000 + 3600000, true}); publish();
  EXPECT_EQ(service->mappedToken(*meeting, false)->error, ServiceError::AmbiguousOccurrence);
  snapshot.active = false; publish();
  EXPECT_EQ(service->mappedToken(*meeting, false)->error, ServiceError::OccurrenceUnavailable);
}

TEST_F(SeriesServiceTest, ConcurrentClientsAndPractitionersCreateExactlyOneRoom) {
  auto invitation = invite(); ASSERT_TRUE(invitation.ok());
  std::vector<std::future<Result<TokenResult>>> requests;
  for (int i = 0; i < 12; ++i) requests.push_back(std::async(std::launch::async, [&, i] {
    return i % 2 ? service->specialistToken(bearer, uid, now * 1000)
                 : *service->clientToken(invitation.value->code, invitation.value->passcode);
  }));
  std::set<std::string> rooms;
  for (auto &request : requests) { auto response = request.get(); ASSERT_TRUE(response.ok()); rooms.insert(response.value->roomName); }
  EXPECT_EQ(rooms.size(), 1);
}

TEST_F(SeriesServiceTest, InvitationAndTokenRateLimitsAreIndependentAndRecover) {
  auto invitation = invite(); ASSERT_TRUE(invitation.ok());
  for (int i = 1; i < 10; ++i) EXPECT_TRUE(invite().ok());
  EXPECT_EQ(invite().error, ServiceError::TooManyAttempts);
  for (int i = 0; i < 30; ++i) EXPECT_TRUE(service->specialistToken(bearer, uid, now * 1000).ok());
  EXPECT_EQ(service->specialistToken(bearer, uid, now * 1000).error, ServiceError::TooManyAttempts);
  EXPECT_TRUE(service->clientToken(invitation.value->code, invitation.value->passcode)->ok());
  now += 60; EXPECT_TRUE(invite().ok()); EXPECT_TRUE(service->specialistToken(bearer, uid, 1791298800000).ok());
}

TEST_F(SeriesServiceTest, ReplayExpiresWithoutRecreatingSecretsAndIsEncryptedAtRest) {
  auto invitation = invite(); ASSERT_TRUE(invitation.ok());
  SeriesRepository repository(conn); auto replay = repository.replay(account, key); ASSERT_TRUE(replay);
  EXPECT_EQ(replay->encryptedPayload.find(invitation.value->code), std::string::npos);
  EXPECT_EQ(replay->encryptedPayload.find(invitation.value->passcode), std::string::npos);
  now += 86400;
  EXPECT_EQ(invite().error, ServiceError::ReplayExpired);
  ASSERT_TRUE(repository.replay(account, key)); EXPECT_TRUE(repository.replay(account, key)->encryptedPayload.empty());
  EXPECT_EQ(invite("33333333-3333-4333-8333-333333333333").error, ServiceError::InvitationExists);
}

TEST_F(SeriesServiceTest, MappedLegacyReissueIsRejectedAndInvalidateKeepsRoomClosed) {
  InvitationsRepository legacy(conn);
  MeetingService facade(auth, meetings, legacy, config, "wss://test", [this] { return now.load(); });
  ASSERT_TRUE(service->specialistToken(bearer, uid, now * 1000).ok());
  SeriesRepository repository(conn);
  auto meeting = meetings.findById(*repository.meetingId(uid, now * 1000)); ASSERT_TRUE(meeting);
  EXPECT_EQ(facade.reissueInvitation(bearer, meeting->meetingRef).error, ServiceError::InvalidRequest);
  ASSERT_TRUE(facade.invalidateMeeting(bearer, meeting->meetingRef).ok());
  EXPECT_EQ(service->specialistToken(bearer, uid, now * 1000).error, ServiceError::OccurrenceUnavailable);
  EXPECT_EQ(facade.issueSpecialistToken(bearer, meeting->meetingRef).error, ServiceError::OccurrenceUnavailable);
  EXPECT_EQ(repository.meetingId(uid, now * 1000), meeting->id);
}

TEST_F(SeriesServiceTest, StrictInvitationRequestsOwnershipAndIdempotencyScope) {
  EXPECT_EQ(service->invitation("", uid, key, R"({"reissue":false})").error, ServiceError::Unauthorized);
  auto foreign = "Bearer " + accounts.createAccount();
  EXPECT_EQ(service->invitation(foreign, uid, key, R"({"reissue":false})").error, ServiceError::NotFound);
  EXPECT_EQ(service->revoke(foreign, uid).error, ServiceError::NotFound);
  EXPECT_EQ(service->specialistToken(foreign, uid, now * 1000).error, ServiceError::NotFound);
  for (const auto &json : {"{}", "null", "{\"reissue\":0}", "{\"reissue\":true,\"extra\":1}", "{\"reissue\":true,\"reissue\":false}", "{\"reissue\":false}x"}) {
    EXPECT_EQ(service->invitation(bearer, uid, key, json).error, ServiceError::InvalidRequest);
  }
  EXPECT_EQ(service->invitation(bearer, uid, "bad-key", R"({"reissue":false})").error, ServiceError::InvalidRequest);
  auto first = invite(); ASSERT_TRUE(first.ok());
  const std::string other = "44444444-4444-4444-8444-444444444444";
  ASSERT_EQ(schedules.put(account, other, snapshot).status, ScheduleWriteStatus::Applied);
  EXPECT_EQ(service->invitation(bearer, other, key, R"({"reissue":false})").error, ServiceError::IdempotencyConflict);
}

TEST_F(SeriesServiceTest, ReissueReplayReturnsOriginalResultButNeverReactivatesItsCode) {
  auto old = invite(); ASSERT_TRUE(old.ok());
  auto fresh = service->invitation(bearer, uid, "33333333-3333-4333-8333-333333333333", R"({"reissue":true})"); ASSERT_TRUE(fresh.ok());
  auto retry = invite(); ASSERT_TRUE(retry.ok()); EXPECT_EQ(retry.value->code, old.value->code);
  EXPECT_EQ(service->clientToken(retry.value->code, retry.value->passcode)->error, ServiceError::InvitationRevoked);
  EXPECT_TRUE(service->clientToken(fresh.value->code, fresh.value->passcode)->ok());
  config.liveKitApiSecret = "rotated-secret"; restart();
  EXPECT_EQ(invite().error, ServiceError::ReplayExpired);
  // Existing passcode hashes remain usable; old replay ciphertext cannot be decrypted.
  EXPECT_TRUE(service->clientToken(fresh.value->code, fresh.value->passcode)->ok());
}

TEST_F(SeriesServiceTest, RoomInsertAndInvitationReissueFailuresRollbackAllRows) {
  conn.exec("CREATE TRIGGER fail_mapping BEFORE INSERT ON occurrence_meetings BEGIN SELECT RAISE(ABORT,'test'); END;");
  EXPECT_THROW(service->specialistToken(bearer, uid, now * 1000), std::runtime_error);
  sqlite3_stmt *query = nullptr;
  ASSERT_EQ(sqlite3_prepare_v2(conn.raw(), "SELECT count(*) FROM meetings", -1, &query, nullptr), SQLITE_OK);
  ASSERT_EQ(sqlite3_step(query), SQLITE_ROW); EXPECT_EQ(sqlite3_column_int(query, 0), 0); sqlite3_finalize(query);
  conn.exec("DROP TRIGGER fail_mapping");
  auto first = invite(); ASSERT_TRUE(first.ok());
  conn.exec("CREATE TRIGGER fail_replay BEFORE INSERT ON series_invitation_replays BEGIN SELECT RAISE(ABORT,'test'); END;");
  EXPECT_THROW(service->invitation(bearer, uid, "33333333-3333-4333-8333-333333333333", R"({"reissue":true})"), std::runtime_error);
  EXPECT_TRUE(service->clientToken(first.value->code, first.value->passcode)->ok());
  EXPECT_EQ(SeriesRepository(conn).current(uid)->generation, 1);
}

TEST_F(SeriesServiceTest, BackupRestoreAndMigrationPreserveReplayRoomAndLegacyRows) {
  auto first = invite(); ASSERT_TRUE(first.ok());
  auto client = service->clientToken(first.value->code, first.value->passcode); ASSERT_TRUE(client && client->ok());
  auto legacy = meetings.create(account, "2026-10-06T15:00:00Z", "2026-10-06T16:00:00Z");
  InvitationsRepository invitations(conn); auto legacyInvitation = invitations.create(legacy.id, account);
  auto path = std::filesystem::temp_directory_path() / ("sessio-series-" + generateUrlSafeToken(9) + ".sqlite3");
  {
    SqliteConnection disk(path.string());
    auto *backup = sqlite3_backup_init(disk.raw(), "main", conn.raw(), "main"); ASSERT_NE(backup, nullptr);
    EXPECT_EQ(sqlite3_backup_step(backup, -1), SQLITE_DONE); EXPECT_EQ(sqlite3_backup_finish(backup), SQLITE_OK);
  }
  {
    SqliteConnection restored(path.string()); runMigrations(restored);
    AccountsRepository a(restored); StaticTokenAuthorizer auth2(a); MeetingsRepository m(restored);
    SeriesService restoredService(restored, auth2, m, config, "wss://test", [this] { return now.load(); });
    auto replay = restoredService.invitation(bearer, uid, key, R"({"reissue":false})"); ASSERT_TRUE(replay.ok());
    EXPECT_EQ(replay.value->code, first.value->code); EXPECT_EQ(replay.value->passcode, first.value->passcode);
    auto joined = restoredService.clientToken(first.value->code, first.value->passcode); ASSERT_TRUE(joined && joined->ok());
    EXPECT_EQ(joined->value->roomName, client->value->roomName);
    EXPECT_TRUE(m.findByRef(legacy.meetingRef)); EXPECT_TRUE(InvitationsRepository(restored).findByCode(legacyInvitation.invitationCode));
  }
  std::filesystem::remove(path);
}

TEST_F(SeriesServiceTest, SignedTokensUseInjectedServerTimeAndStableClientIdentity) {
  auto invitation = invite(); ASSERT_TRUE(invitation.ok());
  auto first = service->clientToken(invitation.value->code, invitation.value->passcode); ASSERT_TRUE(first && first->ok());
  auto decode = [](const std::string &encoded) {
    std::string decoded(encoded.size(), '\0'); size_t size = 0;
    EXPECT_EQ(sodium_base642bin(reinterpret_cast<unsigned char *>(decoded.data()), decoded.size(), encoded.data(), encoded.size(), nullptr, &size, nullptr, sodium_base64_VARIANT_URLSAFE_NO_PADDING), 0);
    decoded.resize(size); return decoded;
  };
  auto payload = [&](const TokenResult &token) {
    auto dot = token.jwt.find('.'), last = token.jwt.rfind('.');
    auto signature = decode(token.jwt.substr(last + 1));
    unsigned char expected[crypto_auth_hmacsha256_BYTES]; crypto_auth_hmacsha256_state state;
    crypto_auth_hmacsha256_init(&state, reinterpret_cast<const unsigned char *>(config.liveKitApiSecret.data()), config.liveKitApiSecret.size());
    crypto_auth_hmacsha256_update(&state, reinterpret_cast<const unsigned char *>(token.jwt.data()), last);
    crypto_auth_hmacsha256_final(&state, expected);
    EXPECT_EQ(signature, std::string(reinterpret_cast<char *>(expected), sizeof(expected)));
    return decode(token.jwt.substr(dot + 1, last - dot - 1));
  };
  const auto decoded = payload(*first->value);
  EXPECT_NE(decoded.find("\"nbf\":1791298800"), std::string::npos);
  EXPECT_NE(decoded.find("\"exp\":1791299400"), std::string::npos);
  EXPECT_EQ(first->value->expiresAtUnix, 1791299400);
  SeriesRepository repository(conn); auto meeting = meetings.findById(*repository.meetingId(uid, now * 1000)); ASSERT_TRUE(meeting);
  const auto identity = "\"sub\":\"client-" + meeting->meetingRef + "\"";
  EXPECT_NE(decoded.find(identity), std::string::npos);
  now += 10;
  auto second = service->clientToken(invitation.value->code, invitation.value->passcode); ASSERT_TRUE(second && second->ok());
  EXPECT_NE(payload(*second->value).find(identity), std::string::npos);
  EXPECT_EQ(second->value->roomName, first->value->roomName);
  now += 2 * 3600;
  EXPECT_EQ(service->clientToken(invitation.value->code, invitation.value->passcode)->error, ServiceError::OccurrenceUnavailable);
}

TEST_F(SeriesServiceTest, InvitationTokenLimitAndConcurrentSixthFailurePersist) {
  auto invitation = invite(); ASSERT_TRUE(invitation.ok());
  for (int i = 0; i < 30; ++i) ASSERT_TRUE(service->clientToken(invitation.value->code, invitation.value->passcode)->ok());
  EXPECT_EQ(service->clientToken(invitation.value->code, invitation.value->passcode)->error, ServiceError::TooManyAttempts);
  now += 60;
  std::vector<std::future<Result<TokenResult>>> requests;
  for (int i = 0; i < 10; ++i) requests.push_back(std::async(std::launch::async, [&] { return *service->clientToken(invitation.value->code, "wrong"); }));
  int wrong = 0, revoked = 0;
  for (auto &request : requests) { auto r = request.get(); wrong += r.error == ServiceError::WrongPasscode; revoked += r.error == ServiceError::InvitationRevoked; }
  EXPECT_EQ(wrong, 5); EXPECT_EQ(revoked, 5);
  EXPECT_EQ(SeriesRepository(conn).current(uid)->attempts, 6);
}
