#pragma once

#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"

namespace pcm::tokenbackend {

#include OATPP_CODEGEN_BEGIN(DTO)

class CreateMeetingRequestDto : public oatpp::DTO {
  DTO_INIT(CreateMeetingRequestDto, DTO)
  DTO_FIELD(String, scheduledStart);
  DTO_FIELD(String, scheduledEnd);
};

class CreateMeetingResponseDto : public oatpp::DTO {
  DTO_INIT(CreateMeetingResponseDto, DTO)
  DTO_FIELD(String, meetingRef);
  DTO_FIELD(String, invitationUrl);
  DTO_FIELD(String, passcode);
  DTO_FIELD(String, scheduledStart);
  DTO_FIELD(String, scheduledEnd);
};

// Same shape as CreateMeetingResponseDto: a re-issued invitation is the same
// meeting with a fresh code and passcode, so callers can reuse one parser.
class ReissueInvitationResponseDto : public oatpp::DTO {
  DTO_INIT(ReissueInvitationResponseDto, DTO)
  DTO_FIELD(String, meetingRef);
  DTO_FIELD(String, invitationUrl);
  DTO_FIELD(String, passcode);
  DTO_FIELD(String, scheduledStart);
  DTO_FIELD(String, scheduledEnd);
};

class TokenResponseDto : public oatpp::DTO {
  DTO_INIT(TokenResponseDto, DTO)
  DTO_FIELD(String, endpointUrl);
  DTO_FIELD(String, roomName);
  DTO_FIELD(String, token);
  DTO_FIELD(Int64, expiresAt);
};

class ClientTokenRequestDto : public oatpp::DTO {
  DTO_INIT(ClientTokenRequestDto, DTO)
  DTO_FIELD(String, passcode);
};

class ErrorResponseDto : public oatpp::DTO {
  DTO_INIT(ErrorResponseDto, DTO)
  DTO_FIELD(String, error);
};

#include OATPP_CODEGEN_END(DTO)

} // namespace pcm::tokenbackend
