#include "service/meeting_service.h"

#include "crypto/hashing.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>

namespace pcm::tokenbackend {

namespace {

// ADR-12: the invitation's lifetime is the meeting's scheduled window — start
// minus a short pre-join buffer, through end plus a short grace period — not
// an indefinite validity that only ends on explicit invalidation.
constexpr int64_t kPreJoinBufferSeconds = 5 * 60;
constexpr int64_t kGracePeriodSeconds = 15 * 60;

// Parses the ISO 8601 UTC form the repositories write ("%Y-%m-%dT%H:%M:%SZ",
// see nowIso8601() in meetings_repository.cpp / invitations_repository.cpp).
// Anything trailing the seconds field (the "Z", a fractional part) is ignored,
// so a caller that stored "…T10:00:00" or "…T10:00:00.000Z" still parses.
std::optional<int64_t> parseIso8601Utc(const std::string &value) {
  std::tm tm{};
  std::istringstream in(value);
  in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
  if (in.fail()) {
    return std::nullopt;
  }
  // timegm() interprets the fields as UTC; std::mktime() would apply the
  // host's local timezone and silently shift every window by the UTC offset.
  const std::time_t seconds = timegm(&tm);
  if (seconds == static_cast<std::time_t>(-1)) {
    return std::nullopt;
  }
  return static_cast<int64_t>(seconds);
}

int64_t nowUnixSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// Returns the error to report for this meeting, or nullopt if a token may be
// issued for it right now. Covers both the explicit-invalidation path (status)
// and the ADR-12 scheduled-window path.
std::optional<ServiceError> meetingUsabilityError(const Meeting &meeting) {
  if (meeting.status != "active") {
    return ServiceError::MeetingWindowClosed;
  }

  auto start = parseIso8601Utc(meeting.scheduledStart);
  auto end = parseIso8601Utc(meeting.scheduledEnd);
  if (!start || !end) {
    // Fail closed: an unparsable window cannot be shown to have opened, and
    // this is the boundary that bounds an invitation's lifetime.
    return ServiceError::MeetingWindowClosed;
  }

  const int64_t now = nowUnixSeconds();
  if (now < *start - kPreJoinBufferSeconds || now > *end + kGracePeriodSeconds) {
    return ServiceError::MeetingWindowClosed;
  }
  return std::nullopt;
}

} // namespace

Result<MeetingService::CreateMeetingOutcome>
MeetingService::createMeeting(const std::string &bearerCredential,
                               const std::string &scheduledStart,
                               const std::string &scheduledEnd) {
  auto accountId = authorizer_.authorize(bearerCredential);
  if (!accountId) {
    return {std::nullopt, ServiceError::Unauthorized};
  }

  auto meeting = meetings_.create(*accountId, scheduledStart, scheduledEnd);
  auto invitation = invitations_.create(meeting.id, *accountId);

  CreateMeetingOutcome outcome;
  outcome.meetingRef = meeting.meetingRef;
  outcome.invitationCode = invitation.invitationCode;
  outcome.passcode = invitation.passcode;
  outcome.scheduledStart = meeting.scheduledStart;
  outcome.scheduledEnd = meeting.scheduledEnd;
  return {outcome, std::nullopt};
}

Result<MeetingService::ReissueInvitationOutcome>
MeetingService::reissueInvitation(const std::string &bearerCredential,
                                   const std::string &meetingRef) {
  auto accountId = authorizer_.authorize(bearerCredential);
  if (!accountId) {
    return {std::nullopt, ServiceError::Unauthorized};
  }

  auto meeting = meetings_.findByRef(meetingRef);
  if (!meeting || meeting->accountId != *accountId) {
    return {std::nullopt, ServiceError::NotFound};
  }
  if (meeting->status != "active") {
    // An explicitly invalidated meeting stays dead; re-issuing an invitation
    // for it would quietly undo the practitioner's invalidate call.
    return {std::nullopt, ServiceError::MeetingWindowClosed};
  }
  // Deliberately does NOT require the scheduled window to be open: the whole
  // point is to recover a locked-out or leaked invitation, which a
  // practitioner will usually do while preparing for an upcoming session.

  auto invitation = invitations_.reissueForMeeting(meeting->id, *accountId);

  ReissueInvitationOutcome outcome;
  outcome.meetingRef = meeting->meetingRef;
  outcome.invitationCode = invitation.invitationCode;
  outcome.passcode = invitation.passcode;
  outcome.scheduledStart = meeting->scheduledStart;
  outcome.scheduledEnd = meeting->scheduledEnd;
  return {outcome, std::nullopt};
}

Result<TokenResult> MeetingService::issueSpecialistToken(const std::string &bearerCredential,
                                                           const std::string &meetingRef) {
  auto accountId = authorizer_.authorize(bearerCredential);
  if (!accountId) {
    return {std::nullopt, ServiceError::Unauthorized};
  }

  auto meeting = meetings_.findByRef(meetingRef);
  if (!meeting || meeting->accountId != *accountId) {
    return {std::nullopt, ServiceError::NotFound};
  }
  if (auto usability = meetingUsabilityError(*meeting)) {
    return {std::nullopt, *usability};
  }

  VideoGrants grants;
  grants.room = meeting->roomName;
  std::string identity = "practitioner-" + meeting->meetingRef;
  auto jwt = mintLiveKitJwt(config_.liveKitApiKey, config_.liveKitApiSecret, identity, grants,
                             config_.tokenTtlSeconds);

  auto now = std::chrono::system_clock::now();
  auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

  TokenResult result;
  result.endpointUrl = liveKitEndpointUrl_;
  result.roomName = meeting->roomName;
  result.jwt = jwt;
  result.expiresAtUnix = nowSeconds + config_.tokenTtlSeconds;
  return {result, std::nullopt};
}

Result<TokenResult> MeetingService::issueClientToken(const std::string &invitationCode,
                                                       const std::string &passcode) {
  auto invitation = invitations_.findByCode(invitationCode);
  if (!invitation) {
    return {std::nullopt, ServiceError::NotFound};
  }
  if (invitation->status != "active") {
    // An invitation leaves "active" two ways: the attempt budget ran out
    // (recordFailedPasscodeAttempt auto-invalidates at 5), or it was
    // superseded by reissueInvitation. The attempt counter distinguishes
    // them, so the caller is told which actually happened instead of always
    // seeing "too many attempts".
    if (invitation->passcodeAttempts >= 5) {
      return {std::nullopt, ServiceError::TooManyAttempts};
    }
    return {std::nullopt, ServiceError::MeetingWindowClosed};
  }

  // The meeting window is checked *before* the passcode so that requests
  // arriving outside it neither burn a passcode attempt nor pay for an
  // Argon2id verification. The caller already holds the invitation code, so
  // learning that the window is shut tells them nothing new.
  auto meeting = meetings_.findById(invitation->meetingId);
  if (!meeting) {
    return {std::nullopt, ServiceError::NotFound};
  }
  if (auto usability = meetingUsabilityError(*meeting)) {
    return {std::nullopt, *usability};
  }

  if (!passcodeMatches(passcode, invitation->passcodeHash)) {
    int attempts = invitations_.recordFailedPasscodeAttempt(invitation->id);
    if (attempts >= 5) {
      return {std::nullopt, ServiceError::TooManyAttempts};
    }
    return {std::nullopt, ServiceError::WrongPasscode};
  }

  VideoGrants grants;
  grants.room = meeting->roomName;
  std::string identity = "client-" + meeting->meetingRef;
  auto jwt = mintLiveKitJwt(config_.liveKitApiKey, config_.liveKitApiSecret, identity, grants,
                             config_.tokenTtlSeconds);

  auto now = std::chrono::system_clock::now();
  auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

  TokenResult result;
  result.endpointUrl = liveKitEndpointUrl_;
  result.roomName = meeting->roomName;
  result.jwt = jwt;
  result.expiresAtUnix = nowSeconds + config_.tokenTtlSeconds;
  return {result, std::nullopt};
}

Result<std::monostate> MeetingService::invalidateMeeting(const std::string &bearerCredential,
                                                           const std::string &meetingRef) {
  auto accountId = authorizer_.authorize(bearerCredential);
  if (!accountId) {
    return {std::nullopt, ServiceError::Unauthorized};
  }

  auto meeting = meetings_.findByRef(meetingRef);
  if (!meeting || meeting->accountId != *accountId) {
    return {std::nullopt, ServiceError::NotFound};
  }

  meetings_.invalidate(meeting->id);
  return {std::monostate{}, std::nullopt};
}

} // namespace pcm::tokenbackend
