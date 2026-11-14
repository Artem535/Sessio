#pragma once
#include "controller/meetings_controller.h"
#include "service/series_service.h"
#include "service/schedule_wire.h"

namespace pcm::tokenbackend {
// Small bodies, including chunked requests, are bounded before decoding JSON.
class SeriesBodyTooLarge : public std::exception {};
class SeriesBodyWriter : public oatpp::data::stream::WriteCallback {
public:
  std::string body;
  oatpp::v_io_size write(const void *data, v_buff_size count, oatpp::async::Action &) override {
    if (count < 0 || static_cast<size_t>(count) > 4096 - body.size()) throw SeriesBodyTooLarge{};
    body.append(static_cast<const char *>(data), count); return count;
  }
};
#include OATPP_CODEGEN_BEGIN(ApiController)
class SeriesController : public oatpp::web::server::api::ApiController {
public:
  SeriesController(const std::shared_ptr<ObjectMapper> &mapper, SeriesService &service)
      : oatpp::web::server::api::ApiController(mapper), service_(service) {}
  ENDPOINT("POST", "/v1/schedule-series/{series_uid}/invitation", invitation,
           PATH(String, series_uid), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    try {
      SeriesBodyWriter writer; request->transferBody(&writer);
      auto key = request->getHeader("Idempotency-Key");
      auto result = service_.invitation(MeetingsController::authCredentialOf(request), *series_uid,
                                        toStdString(key), writer.body);
      if (!result.ok()) return error(*result.error);
      auto &v = *result.value;
      return json(Status::CODE_200, "{\"invitation_url\":" + jsonQuote(v.invitationUrl) +
          ",\"passcode\":" + jsonQuote(v.passcode) + ",\"generation\":" + std::to_string(v.generation) + "}");
    } catch (const SeriesBodyTooLarge &) { return tooLarge(); }
      catch (...) { return json(Status::CODE_500, "{\"error\":\"internal_error\"}"); }
  }
  ENDPOINT("POST", "/v1/schedule-series/{series_uid}/revoke", revoke,
           PATH(String, series_uid), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    try {
      SeriesBodyWriter writer; request->transferBody(&writer);
      if (!writer.body.empty() && writer.body != "{}") return error(ServiceError::InvalidRequest);
      auto result = service_.revoke(MeetingsController::authCredentialOf(request), *series_uid);
      return result.ok() ? json(Status::CODE_204, "") : error(*result.error);
    } catch (const SeriesBodyTooLarge &) { return tooLarge(); }
      catch (...) { return json(Status::CODE_500, "{\"error\":\"internal_error\"}"); }
  }
  ENDPOINT("POST", "/v1/schedule-series/{series_uid}/occurrences/{original_start_utc}/specialist-token", specialist,
           PATH(String, series_uid), PATH(String, original_start_utc), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    try {
      SeriesBodyWriter writer; request->transferBody(&writer);
      std::string name;
      if (!writer.body.empty()) {
        try { name = parseSpecialistNameJson(writer.body); }
        catch (const std::invalid_argument &) { return error(ServiceError::InvalidRequest); }
      }
      int64_t original;
      try { original = parseUtcTimestamp(*original_start_utc); }
      catch (...) { return error(ServiceError::InvalidRequest); }
      auto result = service_.specialistToken(MeetingsController::authCredentialOf(request), *series_uid, original, name);
      if (!result.ok()) return error(*result.error);
      auto &v = *result.value;
      return json(Status::CODE_200, "{\"endpointUrl\":" + jsonQuote(v.endpointUrl) + ",\"roomName\":" + jsonQuote(v.roomName) +
          ",\"token\":" + jsonQuote(v.jwt) + ",\"expiresAt\":" + std::to_string(v.expiresAtUnix) + "}");
    } catch (const SeriesBodyTooLarge &) { return tooLarge(); }
      catch (...) { return json(Status::CODE_500, "{\"error\":\"internal_error\"}"); }
  }
private:
  SeriesService &service_;
  std::shared_ptr<OutgoingResponse> json(const Status &status, const std::string &body) {
    auto result = createResponse(status, body.c_str());
    result->putHeader("Content-Type", "application/json"); result->putHeader("Cache-Control", "no-store"); return result;
  }
  std::shared_ptr<OutgoingResponse> error(ServiceError code) {
    auto response = json(statusForError(code), "{\"error\":" + jsonQuote(serviceErrorName(code)) + "}");
    if (code == ServiceError::TooManyAttempts) response->putHeader("Retry-After", "60"); return response;
  }
  std::shared_ptr<OutgoingResponse> tooLarge() {
    auto result = json(Status::CODE_413, "{\"error\":\"invalid_request\"}"); result->putHeader("Connection", "close"); return result;
  }
};
#include OATPP_CODEGEN_END(ApiController)
}
