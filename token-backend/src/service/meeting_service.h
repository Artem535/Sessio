#pragma once

#include "auth/authorizer.h"
#include "config.h"
#include "crypto/livekit_jwt.h"
#include "db/invitations_repository.h"
#include "db/meetings_repository.h"

#include <optional>
#include <string>
#include <variant>

namespace pcm::tokenbackend {

struct TokenResult {
  std::string endpointUrl;
  std::string roomName;
  std::string jwt;
  int64_t expiresAtUnix;
};

enum class ServiceError {
  Unauthorized,
  NotFound,
  WrongPasscode,
  TooManyAttempts,
  MeetingWindowClosed,
};

template <typename T> struct Result {
  std::optional<T> value;
  std::optional<ServiceError> error;
  bool ok() const { return value.has_value(); }
};

class MeetingService {
public:
  MeetingService(Authorizer &authorizer, MeetingsRepository &meetings,
                  InvitationsRepository &invitations, const Config &config,
                  std::string liveKitEndpointUrl)
      : authorizer_(authorizer), meetings_(meetings), invitations_(invitations),
        config_(config), liveKitEndpointUrl_(std::move(liveKitEndpointUrl)) {}

  struct CreateMeetingOutcome {
    std::string meetingRef;
    std::string invitationCode;
    std::string passcode;
    std::string scheduledStart;
    std::string scheduledEnd;
  };
  Result<CreateMeetingOutcome> createMeeting(const std::string &bearerCredential,
                                              const std::string &scheduledStart,
                                              const std::string &scheduledEnd);

  // Retires every invitation on an existing meeting and mints a fresh one.
  // The escape hatch from ADR-12's permanent 5-failed-attempt lockout: without
  // it the only recovery is creating a whole new meeting, which changes the
  // meeting_ref and room and orphans any client-side state tied to the old one.
  struct ReissueInvitationOutcome {
    std::string meetingRef;
    std::string invitationCode;
    std::string passcode;
    std::string scheduledStart;
    std::string scheduledEnd;
  };
  Result<ReissueInvitationOutcome> reissueInvitation(const std::string &bearerCredential,
                                                      const std::string &meetingRef);

  Result<TokenResult> issueSpecialistToken(const std::string &bearerCredential,
                                            const std::string &meetingRef);

  Result<TokenResult> issueClientToken(const std::string &invitationCode,
                                        const std::string &passcode);

  Result<std::monostate> invalidateMeeting(const std::string &bearerCredential,
                                            const std::string &meetingRef);

private:
  Authorizer &authorizer_;
  MeetingsRepository &meetings_;
  InvitationsRepository &invitations_;
  const Config &config_;
  std::string liveKitEndpointUrl_;
};

} // namespace pcm::tokenbackend
