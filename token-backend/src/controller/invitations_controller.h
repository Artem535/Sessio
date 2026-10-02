#pragma once

#include "controller/dto.h"
#include "controller/meetings_controller.h" // for statusForError
#include "controller/request_log.h"
#include "service/meeting_service.h"
#include "controller/series_controller.h"

#include "oatpp/web/server/api/ApiController.hpp"
#include "oatpp/core/macro/codegen.hpp"

namespace pcm::tokenbackend {

#include OATPP_CODEGEN_BEGIN(ApiController)

class InvitationsController : public oatpp::web::server::api::ApiController {
public:
  InvitationsController(const std::shared_ptr<ObjectMapper> &objectMapper,
                         MeetingService &service)
      : oatpp::web::server::api::ApiController(objectMapper), service_(service) {}

  ENDPOINT("POST", "/v1/invitations/{code}/client-token", clientToken, PATH(String, code),
            REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    // A body of `{}` would otherwise arrive at the service as an empty-string
    // passcode and be scored as a wrong guess, letting anyone holding the
    // invitation code exhaust ADR-12's 5-attempt budget — permanently
    // invalidating the invitation — with six empty requests. A malformed
    // request is a 400 and must never touch the attempt counter.
    // The live request path embeds the invitation code, which is a secret, so
    // every log line below uses this fixed route template and an empty
    // subject. Never log `code` or the passcode.
    static constexpr const char *kRoute = "/v1/invitations/{code}/client-token";

    oatpp::Object<ClientTokenRequestDto> body;
    try {
      SeriesBodyWriter writer; request->transferBody(&writer);
      body = getDefaultObjectMapper()->readFromString<oatpp::Object<ClientTokenRequestDto>>(writer.body.c_str());
    } catch (const SeriesBodyTooLarge &) {
      auto response = createResponse(Status::CODE_413, "{\"error\":\"invalid_request\"}");
      response->putHeader("Cache-Control", "no-store"); response->putHeader("Connection", "close");
      response->putHeader("Content-Type", "application/json"); return response;
    } catch (...) {
      auto response = createResponse(Status::CODE_400, "{\"error\":\"invalid_request\"}");
      response->putHeader("Cache-Control", "no-store"); response->putHeader("Content-Type", "application/json"); return response;
    }

    if (!body || !body->passcode || body->passcode->empty()) {
      logRequestFailure("POST", kRoute, "", Status::CODE_400.code, "passcode_required");
      auto err = ErrorResponseDto::createShared();
      err->error = "passcode_required";
      auto response = createDtoResponse(Status::CODE_400, err); response->putHeader("Cache-Control", "no-store"); return response;
    }

    Result<TokenResult> result;
    try {
      result = service_.issueClientToken(toStdString(code), toStdString(body->passcode), toStdString(body->displayName));
    } catch (...) {
      auto response = createResponse(Status::CODE_500, "{\"error\":\"internal_error\"}");
      response->putHeader("Cache-Control", "no-store"); response->putHeader("Content-Type", "application/json"); return response;
    }
    if (!result.ok()) {
      auto status = statusForError(*result.error);
      logServiceFailure("POST", kRoute, "", status.code, *result.error);
      auto err = ErrorResponseDto::createShared();
      err->error = serviceErrorName(*result.error);
      auto response = createDtoResponse(status, err); response->putHeader("Cache-Control", "no-store");
      if (*result.error == ServiceError::TooManyAttempts) response->putHeader("Retry-After", "60"); return response;
    }
    // Log the room, not the code, the passcode or the JWT.
    logRequestOk("POST", kRoute, result.value->roomName, Status::CODE_200.code);
    auto dto = TokenResponseDto::createShared();
    dto->endpointUrl = result.value->endpointUrl;
    dto->roomName = result.value->roomName;
    dto->token = result.value->jwt;
    dto->expiresAt = result.value->expiresAtUnix;
    auto response = createDtoResponse(Status::CODE_200, dto); response->putHeader("Cache-Control", "no-store"); return response;
  }

private:
  MeetingService &service_;
};

#include OATPP_CODEGEN_END(ApiController)

} // namespace pcm::tokenbackend
