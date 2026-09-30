#include "service/meeting_service.h"

#include "auth/static_token_authorizer.h"
#include "config.h"
#include "db/accounts_repository.h"
#include "db/invitations_repository.h"
#include "db/meetings_repository.h"
#include "db/migrations.h"
#include "db/sqlite_connection.h"

#include <gtest/gtest.h>
#include <sodium.h>

#include <chrono>
#include <barrier>
#include <ctime>
#include <future>
#include <set>
#include <vector>

namespace {

std::string decodeJwtPart(const std::string &part) {
  std::string decoded(part.size(), '\0');
  size_t size = 0;
  if (sodium_base642bin(reinterpret_cast<unsigned char *>(decoded.data()), decoded.size(),
                        part.data(), part.size(), nullptr, &size, nullptr,
                        sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0) {
    return {};
  }
  decoded.resize(size);
  return decoded;
}

std::string verifiedPayload(const std::string &jwt, const std::string &secret) {
  const auto first = jwt.find('.');
  const auto second = jwt.find('.', first + 1);
  const auto signature = decodeJwtPart(jwt.substr(second + 1));
  unsigned char expected[crypto_auth_hmacsha256_BYTES];
  crypto_auth_hmacsha256_state state;
  crypto_auth_hmacsha256_init(&state, reinterpret_cast<const unsigned char *>(secret.data()), secret.size());
  crypto_auth_hmacsha256_update(&state, reinterpret_cast<const unsigned char *>(jwt.data()), second);
  crypto_auth_hmacsha256_final(&state, expected);
  EXPECT_EQ(signature.size(), sizeof(expected));
  if (signature.size() == sizeof(expected)) {
    EXPECT_EQ(sodium_memcmp(signature.data(), expected, sizeof(expected)), 0);
  }
  return decodeJwtPart(jwt.substr(first + 1, second - first - 1));
}

std::string subject(const std::string &payload) {
  const auto start = payload.find("\"sub\":\"") + 7;
  return payload.substr(start, payload.find('"', start) - start);
}

// Mirrors the production nowIso8601() format
// ("%Y-%m-%dT%H:%M:%SZ", see meetings_repository.cpp) but offset from now, so
// a test can place a meeting's scheduled window relative to the current time.
std::string isoFromNow(int64_t offsetSeconds) {
  auto when = std::chrono::system_clock::now() + std::chrono::seconds(offsetSeconds);
  std::time_t t = std::chrono::system_clock::to_time_t(when);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

} // namespace

class MeetingServiceTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(sodium_init() >= 0, true);
    conn = std::make_unique<pcm::tokenbackend::SqliteConnection>(":memory:");
    pcm::tokenbackend::runMigrations(*conn);

    accounts = std::make_unique<pcm::tokenbackend::AccountsRepository>(*conn);
    credential = accounts->seedAccount();
    authorizer = std::make_unique<pcm::tokenbackend::StaticTokenAuthorizer>(*accounts);
    meetings = std::make_unique<pcm::tokenbackend::MeetingsRepository>(*conn);
    invitations = std::make_unique<pcm::tokenbackend::InvitationsRepository>(*conn);

    config.liveKitApiKey = "test-key";
    config.liveKitApiSecret = "test-secret";
    config.tokenTtlSeconds = 600;

    service = std::make_unique<pcm::tokenbackend::MeetingService>(
        *authorizer, *meetings, *invitations, config, "ws://livekit.test:7880");
  }

  std::unique_ptr<pcm::tokenbackend::SqliteConnection> conn;
  std::unique_ptr<pcm::tokenbackend::AccountsRepository> accounts;
  std::unique_ptr<pcm::tokenbackend::Authorizer> authorizer;
  std::unique_ptr<pcm::tokenbackend::MeetingsRepository> meetings;
  std::unique_ptr<pcm::tokenbackend::InvitationsRepository> invitations;
  std::unique_ptr<pcm::tokenbackend::MeetingService> service;
  pcm::tokenbackend::Config config{};
  std::string credential;

  // A window that is open right now: started a minute ago, ends in an hour.
  const std::string windowStart = isoFromNow(-60);
  const std::string windowEnd = isoFromNow(60 * 60);
};

TEST_F(MeetingServiceTest, CreateMeetingRejectsBadCredential) {
  auto result = service->createMeeting("wrong-credential", windowStart, windowEnd);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.error, pcm::tokenbackend::ServiceError::Unauthorized);
}

TEST_F(MeetingServiceTest, CreateMeetingSucceedsWithGoodCredential) {
  auto result =
      service->createMeeting(credential, windowStart, windowEnd);
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result.value->meetingRef.empty());
  EXPECT_FALSE(result.value->invitationCode.empty());
  EXPECT_EQ(result.value->passcode.size(), 6u);
}

TEST_F(MeetingServiceTest, SpecialistTokenReturnsSameRoomAsCreatedMeeting) {
  auto created =
      service->createMeeting(credential, windowStart, windowEnd);
  ASSERT_TRUE(created.ok());

  auto token = service->issueSpecialistToken(credential, created.value->meetingRef);
  ASSERT_TRUE(token.ok());
  EXPECT_FALSE(token.value->jwt.empty());
  EXPECT_EQ(token.value->endpointUrl, "ws://livekit.test:7880");
}

TEST_F(MeetingServiceTest, SpecialistTokenRejectsBadCredential) {
  auto created =
      service->createMeeting(credential, windowStart, windowEnd);
  ASSERT_TRUE(created.ok());

  auto token = service->issueSpecialistToken("wrong", created.value->meetingRef);
  EXPECT_FALSE(token.ok());
  EXPECT_EQ(token.error, pcm::tokenbackend::ServiceError::Unauthorized);
}

TEST_F(MeetingServiceTest, ClientTokenSucceedsWithCorrectCodeAndPasscode) {
  auto created =
      service->createMeeting(credential, windowStart, windowEnd);
  ASSERT_TRUE(created.ok());

  auto token =
      service->issueClientToken(created.value->invitationCode, created.value->passcode);
  ASSERT_TRUE(token.ok());
  EXPECT_FALSE(token.value->jwt.empty());
}

TEST_F(MeetingServiceTest, ClientTokenRejectsWrongPasscodeAndCountsAttempt) {
  auto created =
      service->createMeeting(credential, windowStart, windowEnd);
  ASSERT_TRUE(created.ok());

  auto result = service->issueClientToken(created.value->invitationCode, "000000");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.error, pcm::tokenbackend::ServiceError::WrongPasscode);
}

TEST_F(MeetingServiceTest, ClientTokenLocksOutAfterFiveWrongPasscodes) {
  auto created =
      service->createMeeting(credential, windowStart, windowEnd);
  ASSERT_TRUE(created.ok());

  for (int i = 0; i < 5; ++i) {
    service->issueClientToken(created.value->invitationCode, "000000");
  }

  auto result =
      service->issueClientToken(created.value->invitationCode, created.value->passcode);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.error, pcm::tokenbackend::ServiceError::TooManyAttempts);
}

TEST_F(MeetingServiceTest, ClientAndSpecialistTokensShareTheSameRoom) {
  auto created =
      service->createMeeting(credential, windowStart, windowEnd);
  ASSERT_TRUE(created.ok());

  auto specialistToken = service->issueSpecialistToken(credential, created.value->meetingRef);
  auto clientToken =
      service->issueClientToken(created.value->invitationCode, created.value->passcode);
  ASSERT_TRUE(specialistToken.ok());
  ASSERT_TRUE(clientToken.ok());
  EXPECT_EQ(specialistToken.value->roomName, clientToken.value->roomName);
}

TEST_F(MeetingServiceTest, RepeatedTokensHaveUniqueSignedIdentitiesAndDisplayRoles) {
  const auto created = service->createMeeting(credential, windowStart, windowEnd);
  ASSERT_TRUE(created.ok());
  std::set<std::string> identities;
  std::string room;
  for (const auto &role : {std::string("client"), std::string("practitioner")}) {
    for (int issuance = 0; issuance < 4; ++issuance) {
      const auto token = role == "client"
          ? service->issueClientToken(created.value->invitationCode, created.value->passcode)
          : service->issueSpecialistToken(credential, created.value->meetingRef);
      ASSERT_TRUE(token.ok());
      const auto payload = verifiedPayload(token.value->jwt, config.liveKitApiSecret);
      const auto identity = subject(payload);
      EXPECT_TRUE(identities.insert(identity).second);
      EXPECT_TRUE(identity.starts_with(role + "-" + created.value->meetingRef + "-"));
      EXPECT_EQ(subject(verifiedPayload(token.value->jwt, config.liveKitApiSecret)), identity);
      EXPECT_NE(payload.find("\"metadata\":\"{\\\"role\\\":\\\"" + role + "\\\"}\""), std::string::npos);
      if (room.empty()) room = token.value->roomName;
      EXPECT_EQ(token.value->roomName, room);
      EXPECT_NE(payload.find("\"room\":\"" + room + "\""), std::string::npos);
      for (const auto *grant : {"roomJoin", "canPublish", "canSubscribe", "canPublishData"}) {
        EXPECT_NE(payload.find(std::string("\"") + grant + "\":true"), std::string::npos);
      }
    }
  }
  EXPECT_EQ(identities.size(), 8u);
}

TEST_F(MeetingServiceTest, ConcurrentClientTokensHaveDistinctSignedIdentitiesInTheSameRoom) {
  const auto created = service->createMeeting(credential, windowStart, windowEnd);
  ASSERT_TRUE(created.ok());
  constexpr int issuances = 4;
  std::barrier start(issuances);
  using TokenOutcome = pcm::tokenbackend::Result<pcm::tokenbackend::TokenResult>;
  std::vector<std::future<TokenOutcome>> tokens;
  for (int issuance = 0; issuance < issuances; ++issuance) {
    tokens.emplace_back(std::async(std::launch::async, [&] {
      start.arrive_and_wait();
      return service->issueClientToken(created.value->invitationCode, created.value->passcode);
    }));
  }
  std::set<std::string> identities;
  std::string room;
  for (auto &pending : tokens) {
    std::optional<TokenOutcome> token;
    ASSERT_NO_THROW(token.emplace(pending.get()));
    ASSERT_TRUE(token->ok());
    const auto payload = verifiedPayload(token->value->jwt, config.liveKitApiSecret);
    EXPECT_TRUE(identities.insert(subject(payload)).second);
    EXPECT_TRUE(subject(payload).starts_with("client-" + created.value->meetingRef + "-"));
    EXPECT_NE(payload.find(R"("metadata":"{\"role\":\"client\"}")"), std::string::npos);
    if (room.empty()) room = token->value->roomName;
    EXPECT_EQ(token->value->roomName, room);
    EXPECT_NE(payload.find("\"room\":\"" + room + "\""), std::string::npos);
    for (const auto *grant : {"roomJoin", "canPublish", "canSubscribe", "canPublishData"}) {
      EXPECT_NE(payload.find(std::string("\"") + grant + "\":true"), std::string::npos);
    }
  }
  EXPECT_EQ(identities.size(), issuances);
}

TEST_F(MeetingServiceTest, InvalidateStopsFurtherTokenIssuance) {
  auto created =
      service->createMeeting(credential, windowStart, windowEnd);
  ASSERT_TRUE(created.ok());

  auto invalidateResult = service->invalidateMeeting(credential, created.value->meetingRef);
  EXPECT_TRUE(invalidateResult.ok());

  auto token = service->issueSpecialistToken(credential, created.value->meetingRef);
  EXPECT_FALSE(token.ok());
  EXPECT_EQ(token.error, pcm::tokenbackend::ServiceError::MeetingWindowClosed);

  auto clientToken =
      service->issueClientToken(created.value->invitationCode, created.value->passcode);
  EXPECT_FALSE(clientToken.ok());
}

TEST_F(MeetingServiceTest, ClientCanReconnectAfterFirstSuccessfulJoin) {
  auto created =
      service->createMeeting(credential, windowStart, windowEnd);
  ASSERT_TRUE(created.ok());

  auto firstJoin =
      service->issueClientToken(created.value->invitationCode, created.value->passcode);
  auto secondJoin =
      service->issueClientToken(created.value->invitationCode, created.value->passcode);

  ASSERT_TRUE(firstJoin.ok());
  ASSERT_TRUE(secondJoin.ok())
      << "invitation code must stay valid for reconnects, not be single-use";
}

// --- Invitation re-issue ----------------------------------------------------
//
// ADR-12's 5-attempt lockout is permanent by design. Without a re-issue path
// the only recovery is a brand-new meeting, which changes meeting_ref and
// room and orphans client-side state.

TEST_F(MeetingServiceTest, ReissueRejectsBadCredential) {
  auto created = service->createMeeting(credential, windowStart, windowEnd);
  ASSERT_TRUE(created.ok());

  auto reissued = service->reissueInvitation("wrong-credential", created.value->meetingRef);
  EXPECT_FALSE(reissued.ok());
  EXPECT_EQ(reissued.error, pcm::tokenbackend::ServiceError::Unauthorized);
}

TEST_F(MeetingServiceTest, ReissueRejectsUnknownMeeting) {
  auto reissued = service->reissueInvitation(credential, "mtg_does_not_exist");
  EXPECT_FALSE(reissued.ok());
  EXPECT_EQ(reissued.error, pcm::tokenbackend::ServiceError::NotFound);
}

TEST_F(MeetingServiceTest, ReissueKeepsTheSameMeetingRefAndRoom) {
  auto created = service->createMeeting(credential, windowStart, windowEnd);
  ASSERT_TRUE(created.ok());
  auto roomBefore = service->issueSpecialistToken(credential, created.value->meetingRef);
  ASSERT_TRUE(roomBefore.ok());

  auto reissued = service->reissueInvitation(credential, created.value->meetingRef);
  ASSERT_TRUE(reissued.ok());
  EXPECT_EQ(reissued.value->meetingRef, created.value->meetingRef);
  EXPECT_EQ(reissued.value->scheduledStart, created.value->scheduledStart);
  EXPECT_EQ(reissued.value->scheduledEnd, created.value->scheduledEnd);

  auto newClientToken =
      service->issueClientToken(reissued.value->invitationCode, reissued.value->passcode);
  ASSERT_TRUE(newClientToken.ok());
  EXPECT_EQ(newClientToken.value->roomName, roomBefore.value->roomName);
}

TEST_F(MeetingServiceTest, ReissueInvalidatesThePreviousInvitation) {
  auto created = service->createMeeting(credential, windowStart, windowEnd);
  ASSERT_TRUE(created.ok());

  auto reissued = service->reissueInvitation(credential, created.value->meetingRef);
  ASSERT_TRUE(reissued.ok());
  EXPECT_NE(reissued.value->invitationCode, created.value->invitationCode);

  auto oldCode =
      service->issueClientToken(created.value->invitationCode, created.value->passcode);
  EXPECT_FALSE(oldCode.ok())
      << "the superseded invitation code must stop working immediately";
  EXPECT_EQ(oldCode.error, pcm::tokenbackend::ServiceError::MeetingWindowClosed);
}

TEST_F(MeetingServiceTest, ReissueRecoversFromTheFiveAttemptLockout) {
  auto created = service->createMeeting(credential, windowStart, windowEnd);
  ASSERT_TRUE(created.ok());

  for (int i = 0; i < 5; ++i) {
    service->issueClientToken(created.value->invitationCode, "000000");
  }
  auto lockedOut =
      service->issueClientToken(created.value->invitationCode, created.value->passcode);
  ASSERT_FALSE(lockedOut.ok());
  ASSERT_EQ(lockedOut.error, pcm::tokenbackend::ServiceError::TooManyAttempts);

  auto reissued = service->reissueInvitation(credential, created.value->meetingRef);
  ASSERT_TRUE(reissued.ok());

  auto afterReissue =
      service->issueClientToken(reissued.value->invitationCode, reissued.value->passcode);
  EXPECT_TRUE(afterReissue.ok()) << "a fresh invitation must start with a clean attempt budget";
}

TEST_F(MeetingServiceTest, ReissueRefusedOnAnInvalidatedMeeting) {
  auto created = service->createMeeting(credential, windowStart, windowEnd);
  ASSERT_TRUE(created.ok());
  ASSERT_TRUE(service->invalidateMeeting(credential, created.value->meetingRef).ok());

  auto reissued = service->reissueInvitation(credential, created.value->meetingRef);
  EXPECT_FALSE(reissued.ok())
      << "re-issuing must not quietly undo an explicit invalidate";
  EXPECT_EQ(reissued.error, pcm::tokenbackend::ServiceError::MeetingWindowClosed);
}

// --- ADR-12 scheduled-window enforcement -----------------------------------
//
// The invitation's lifetime is the meeting's scheduled window (start minus a
// 5-minute pre-join buffer, through end plus a 15-minute grace period), not
// "until someone invalidates it".

TEST_F(MeetingServiceTest, SpecialistTokenRejectedBeforeThePreJoinBuffer) {
  auto created =
      service->createMeeting(credential, isoFromNow(60 * 60), isoFromNow(2 * 60 * 60));
  ASSERT_TRUE(created.ok());

  auto token = service->issueSpecialistToken(credential, created.value->meetingRef);
  EXPECT_FALSE(token.ok());
  EXPECT_EQ(token.error, pcm::tokenbackend::ServiceError::MeetingWindowClosed);
}

TEST_F(MeetingServiceTest, ClientTokenRejectedBeforeThePreJoinBuffer) {
  auto created =
      service->createMeeting(credential, isoFromNow(60 * 60), isoFromNow(2 * 60 * 60));
  ASSERT_TRUE(created.ok());

  auto token =
      service->issueClientToken(created.value->invitationCode, created.value->passcode);
  EXPECT_FALSE(token.ok());
  EXPECT_EQ(token.error, pcm::tokenbackend::ServiceError::MeetingWindowClosed);
}

TEST_F(MeetingServiceTest, SpecialistTokenRejectedAfterTheGracePeriod) {
  auto created =
      service->createMeeting(credential, isoFromNow(-2 * 60 * 60), isoFromNow(-60 * 60));
  ASSERT_TRUE(created.ok());

  auto token = service->issueSpecialistToken(credential, created.value->meetingRef);
  EXPECT_FALSE(token.ok());
  EXPECT_EQ(token.error, pcm::tokenbackend::ServiceError::MeetingWindowClosed);
}

TEST_F(MeetingServiceTest, ClientTokenRejectedAfterTheGracePeriod) {
  auto created =
      service->createMeeting(credential, isoFromNow(-2 * 60 * 60), isoFromNow(-60 * 60));
  ASSERT_TRUE(created.ok());

  auto token =
      service->issueClientToken(created.value->invitationCode, created.value->passcode);
  EXPECT_FALSE(token.ok());
  EXPECT_EQ(token.error, pcm::tokenbackend::ServiceError::MeetingWindowClosed);
}

TEST_F(MeetingServiceTest, TokensIssuedInsideTheScheduledWindow) {
  auto created = service->createMeeting(credential, isoFromNow(-60), isoFromNow(30 * 60));
  ASSERT_TRUE(created.ok());

  auto specialistToken = service->issueSpecialistToken(credential, created.value->meetingRef);
  ASSERT_TRUE(specialistToken.ok());

  auto clientToken =
      service->issueClientToken(created.value->invitationCode, created.value->passcode);
  ASSERT_TRUE(clientToken.ok());
}

TEST_F(MeetingServiceTest, TokensIssuedWithinThePreJoinBuffer) {
  // Two minutes before start — inside the 5-minute pre-join buffer.
  auto created = service->createMeeting(credential, isoFromNow(2 * 60), isoFromNow(50 * 60));
  ASSERT_TRUE(created.ok());

  auto clientToken =
      service->issueClientToken(created.value->invitationCode, created.value->passcode);
  EXPECT_TRUE(clientToken.ok());
}

TEST_F(MeetingServiceTest, TokensIssuedWithinTheGracePeriod) {
  // Ended five minutes ago — inside the 15-minute grace period.
  auto created = service->createMeeting(credential, isoFromNow(-60 * 60), isoFromNow(-5 * 60));
  ASSERT_TRUE(created.ok());

  auto clientToken =
      service->issueClientToken(created.value->invitationCode, created.value->passcode);
  EXPECT_TRUE(clientToken.ok());
}

TEST_F(MeetingServiceTest, ClosedWindowDoesNotBurnAPasscodeAttempt) {
  auto created =
      service->createMeeting(credential, isoFromNow(60 * 60), isoFromNow(2 * 60 * 60));
  ASSERT_TRUE(created.ok());

  for (int i = 0; i < 10; ++i) {
    auto attempt = service->issueClientToken(created.value->invitationCode, "000000");
    EXPECT_EQ(attempt.error, pcm::tokenbackend::ServiceError::MeetingWindowClosed);
  }

  auto invitation = invitations->findByCode(created.value->invitationCode);
  ASSERT_TRUE(invitation.has_value());
  EXPECT_EQ(invitation->passcodeAttempts, 0);
  EXPECT_EQ(invitation->status, "active");
}
