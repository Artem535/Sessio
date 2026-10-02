#pragma once

#include "controller/dto.h"
#include "controller/invitation_url.h"
#include "controller/request_log.h"
#include "service/meeting_service.h"

#include "oatpp/web/server/api/ApiController.hpp"
#include "oatpp/core/macro/codegen.hpp"

namespace pcm::tokenbackend {

inline std::string toStdString(const oatpp::String &value) {
  return value ? *value : std::string();
}

inline oatpp::web::protocol::http::Status statusForError(ServiceError err) {
  switch (err) {
  case ServiceError::Unauthorized:
    return oatpp::web::protocol::http::Status::CODE_401;
  case ServiceError::NotFound:
    return oatpp::web::protocol::http::Status::CODE_404;
  case ServiceError::WrongPasscode:
    return oatpp::web::protocol::http::Status::CODE_401;
  case ServiceError::TooManyAttempts:
    return oatpp::web::protocol::http::Status::CODE_429;
  case ServiceError::MeetingWindowClosed:
    return oatpp::web::protocol::http::Status::CODE_410;
  case ServiceError::InvalidDisplayName:
  case ServiceError::InvalidRequest:
    return oatpp::web::protocol::http::Status::CODE_400;
  case ServiceError::InvitationExists:
  case ServiceError::IdempotencyConflict:
  case ServiceError::OccurrenceUnavailable:
  case ServiceError::AmbiguousOccurrence:
    return oatpp::web::protocol::http::Status::CODE_409;
  case ServiceError::ReplayExpired:
  case ServiceError::InvitationRevoked:
    return oatpp::web::protocol::http::Status::CODE_410;
  case ServiceError::ScheduleUnavailable:
    return oatpp::web::protocol::http::Status::CODE_503;
  }
  return oatpp::web::protocol::http::Status::CODE_500;
}

#include OATPP_CODEGEN_BEGIN(ApiController)

class MeetingsController : public oatpp::web::server::api::ApiController {
public:
  // These endpoints read the Authorization header off the request rather than
  // declaring HEADER(String, authHeader, "Authorization"). oat++'s HEADER macro
  // throws HttpError(400, "Missing HEADER parameter 'Authorization'") when the
  // header is absent, so a request with no credential at all answered 400 with
  // an oat++-shaped body while a request with a *wrong* credential answered a
  // normal 401. Both are "you are not authenticated" and both must be 401 with
  // this service's own error body.
  static std::string authCredentialOf(
      const std::shared_ptr<IncomingRequest> &request) {
    auto header = request->getHeader("Authorization");
    return header ? *header : std::string();
  }

  MeetingsController(const std::shared_ptr<ObjectMapper> &objectMapper,
                      MeetingService &service, const std::string &invitationBaseUrl)
      : oatpp::web::server::api::ApiController(objectMapper), service_(service),
        invitationBaseUrl_(invitationBaseUrl) {}

  ENDPOINT("POST", "/v1/meetings", createMeeting, REQUEST(std::shared_ptr<IncomingRequest>, request),
            BODY_DTO(Object<CreateMeetingRequestDto>, body)) {
    static constexpr const char *kRoute = "/v1/meetings";
    // Without the scheduled window there is nothing to bound the invitation's
    // lifetime against, and the meeting would be created already unusable
    // (an unparsable window fails closed). Refuse it up front instead.
    if (!body || !body->scheduledStart || body->scheduledStart->empty() ||
        !body->scheduledEnd || body->scheduledEnd->empty()) {
      logRequestFailure("POST", kRoute, "", Status::CODE_400.code, "schedule_required");
      auto err = ErrorResponseDto::createShared();
      err->error = "schedule_required";
      return createDtoResponse(Status::CODE_400, err);
    }
    auto result = service_.createMeeting(authCredentialOf(request), toStdString(body->scheduledStart),
                                          toStdString(body->scheduledEnd));
    if (!result.ok()) {
      auto status = statusForError(*result.error);
      logServiceFailure("POST", kRoute, "", status.code, *result.error);
      auto err = ErrorResponseDto::createShared();
      err->error = "unauthorized";
      return createDtoResponse(status, err);
    }
    logRequestOk("POST", kRoute, result.value->meetingRef, Status::CODE_200.code);
    auto dto = CreateMeetingResponseDto::createShared();
    dto->meetingRef = result.value->meetingRef;
    dto->invitationUrl = formatInvitationUrl(invitationBaseUrl_, result.value->invitationCode,
                                             result.value->passcode);
    dto->passcode = result.value->passcode;
    dto->scheduledStart = result.value->scheduledStart;
    dto->scheduledEnd = result.value->scheduledEnd;
    return createDtoResponse(Status::CODE_200, dto);
  }

  ENDPOINT("POST", "/v1/meetings/{meetingRef}/invitation", reissueInvitation,
            PATH(String, meetingRef), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    static constexpr const char *kRoute = "/v1/meetings/{meetingRef}/invitation";
    auto result = service_.reissueInvitation(authCredentialOf(request), toStdString(meetingRef));
    if (!result.ok()) {
      auto status = statusForError(*result.error);
      logServiceFailure("POST", kRoute, toStdString(meetingRef), status.code, *result.error);
      auto err = ErrorResponseDto::createShared();
      err->error = "request_failed";
      return createDtoResponse(status, err);
    }
    logRequestOk("POST", kRoute, result.value->meetingRef, Status::CODE_200.code);
    auto dto = ReissueInvitationResponseDto::createShared();
    dto->meetingRef = result.value->meetingRef;
    dto->invitationUrl = formatInvitationUrl(invitationBaseUrl_, result.value->invitationCode,
                                             result.value->passcode);
    dto->passcode = result.value->passcode;
    dto->scheduledStart = result.value->scheduledStart;
    dto->scheduledEnd = result.value->scheduledEnd;
    return createDtoResponse(Status::CODE_200, dto);
  }

  ENDPOINT("POST", "/v1/meetings/{meetingRef}/specialist-token", specialistToken,
            PATH(String, meetingRef), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    static constexpr const char *kRoute = "/v1/meetings/{meetingRef}/specialist-token";
    std::string displayName;
    const auto rawBody = request->readBodyToString();
    if (rawBody && !rawBody->empty()) {
      try {
        const auto body = getDefaultObjectMapper()->readFromString<oatpp::Object<SpecialistTokenRequestDto>>(rawBody);
        OATPP_ASSERT_HTTP(body, Status::CODE_400, "invalid_request");
        displayName = toStdString(body->displayName);
      } catch (...) {
        return createResponse(Status::CODE_400, "invalid_request");
      }
    }
    Result<TokenResult> result;
    try {
      result = service_.issueSpecialistToken(authCredentialOf(request), toStdString(meetingRef), displayName);
    } catch (...) {
      auto response = createResponse(Status::CODE_500, "{\"error\":\"internal_error\"}");
      response->putHeader("Cache-Control", "no-store"); response->putHeader("Content-Type", "application/json"); return response;
    }
    if (!result.ok()) {
      auto status = statusForError(*result.error);
      logServiceFailure("POST", kRoute, toStdString(meetingRef), status.code, *result.error);
      auto err = ErrorResponseDto::createShared();
      err->error = serviceErrorName(*result.error);
      auto response = createDtoResponse(status, err); response->putHeader("Cache-Control", "no-store");
      if (*result.error == ServiceError::TooManyAttempts) response->putHeader("Retry-After", "60"); return response;
    }
    // The JWT itself is deliberately never logged.
    logRequestOk("POST", kRoute, toStdString(meetingRef), Status::CODE_200.code);
    auto dto = TokenResponseDto::createShared();
    dto->endpointUrl = result.value->endpointUrl;
    dto->roomName = result.value->roomName;
    dto->token = result.value->jwt;
    dto->expiresAt = result.value->expiresAtUnix;
    auto response = createDtoResponse(Status::CODE_200, dto); response->putHeader("Cache-Control", "no-store"); return response;
  }

  ENDPOINT("POST", "/v1/meetings/{meetingRef}/invalidate", invalidateMeeting,
            PATH(String, meetingRef), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    static constexpr const char *kRoute = "/v1/meetings/{meetingRef}/invalidate";
    auto result =
        service_.invalidateMeeting(authCredentialOf(request), toStdString(meetingRef));
    if (!result.ok()) {
      auto status = statusForError(*result.error);
      logServiceFailure("POST", kRoute, toStdString(meetingRef), status.code, *result.error);
      auto err = ErrorResponseDto::createShared();
      err->error = "request_failed";
      return createDtoResponse(status, err);
    }
    logRequestOk("POST", kRoute, toStdString(meetingRef), Status::CODE_204.code);
    return createResponse(Status::CODE_204, "");
  }

private:
  MeetingService &service_;
  std::string invitationBaseUrl_;
};

#include OATPP_CODEGEN_END(ApiController)

} // namespace pcm::tokenbackend
