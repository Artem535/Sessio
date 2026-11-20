#pragma once
#include "service/schedule_service.h"
#include "service/schedule_wire.h"
#include "oatpp/web/server/api/ApiController.hpp"
#include "oatpp/core/macro/codegen.hpp"
#include <stdexcept>

namespace pcm::tokenbackend {

// Decode HTTP framing through oat++, but stop accumulating at the raw byte
// limit, including chunked bodies. Reading into a String first is unbounded.
class ScheduleBodyTooLarge : public std::exception {};
class ScheduleBodyWriter : public oatpp::data::stream::WriteCallback {
public:
  std::string body;
  oatpp::v_io_size write(const void *data, v_buff_size count, oatpp::async::Action &) override {
    if (count < 0 || static_cast<size_t>(count) > kMaxScheduleBytes - body.size()) throw ScheduleBodyTooLarge{};
    body.append(static_cast<const char *>(data), static_cast<size_t>(count));
    return count;
  }
};

#include OATPP_CODEGEN_BEGIN(ApiController)
class ScheduleController : public oatpp::web::server::api::ApiController {
public:
  ScheduleController(const std::shared_ptr<ObjectMapper> &mapper, ScheduleService &service)
      : oatpp::web::server::api::ApiController(mapper), service_(service) {}

  ENDPOINT("GET", "/v1/capabilities", capabilities, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    if (!service_.supportsScheduleSeries(credential(request))) return error(ScheduleError::Unauthorized);
    return json(Status::CODE_200, "{\"scheduleSeries\":true}");
  }
  ENDPOINT("GET", "/v1/schedule-series/{series_uid}", getSchedule,
           PATH(String, series_uid), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    try {
      auto result = service_.get(credential(request), *series_uid);
      if (!result.ok()) return error(*result.error, result.reason);
      const auto &stored = *result.value;
      return json(Status::CODE_200, "{\"series_uid\":" + jsonQuote(stored.seriesUid) + ",\"revision\":" +
          std::to_string(stored.snapshot.revision) + ",\"content_hash\":" + jsonQuote(stored.contentHash) +
          ",\"snapshot\":" + scheduleJson(stored.snapshot) + "}");
    } catch (...) { return json(Status::CODE_500, "{\"error\":\"internal_error\"}"); }
  }
  ENDPOINT("PUT", "/v1/schedule-series/{series_uid}", putSchedule,
           PATH(String, series_uid), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    // Authorize before reading attacker-controlled schedule bodies.
    if (!service_.supportsScheduleSeries(credential(request))) {
      auto response = error(ScheduleError::Unauthorized); response->putHeader("Connection", "close"); return response;
    }
    ScheduleBodyWriter writer;
    try {
      request->transferBody(&writer);
      auto result = service_.put(credential(request), *series_uid, writer.body);
      if (!result.ok()) return error(*result.error, result.reason);
      const auto &stored = *result.value;
      return json(Status::CODE_200, "{\"series_uid\":" + jsonQuote(stored.seriesUid) + ",\"revision\":" +
          std::to_string(stored.snapshot.revision) + ",\"content_hash\":" + jsonQuote(stored.contentHash) + "}");
    } catch (const ScheduleBodyTooLarge &) {
      auto response = error(ScheduleError::TooLarge, "payload_too_large");
      response->putHeader("Connection", "close"); return response;
    } catch (...) { return json(Status::CODE_500, "{\"error\":\"internal_error\"}"); }
  }
private:
  ScheduleService &service_;
  static std::string credential(const std::shared_ptr<IncomingRequest> &request) {
    auto h = request->getHeader("Authorization"); return h ? *h : std::string{};
  }
  std::shared_ptr<OutgoingResponse> json(const Status &status, const std::string &body) {
    auto response = createResponse(status, oatpp::String(body.data(), body.size()));
    response->putHeader("Content-Type", "application/json");
    return response;
  }
  std::shared_ptr<OutgoingResponse> error(ScheduleError code, const std::string &reason = {}) {
    Status status = Status::CODE_422; const char *name = "invalid_schedule";
    switch (code) {
    case ScheduleError::Unauthorized: status = Status::CODE_401; name = "unauthorized"; break;
    case ScheduleError::NotFound: status = Status::CODE_404; name = "not_found"; break;
    case ScheduleError::Conflict: status = Status::CODE_409; name = "revision_conflict"; break;
    case ScheduleError::TooLarge: status = Status::CODE_413; break;
    case ScheduleError::Invalid: break;
    case ScheduleError::TooManyRequests: status = Status::CODE_429; name = "too_many_attempts"; break;
    }
    auto response = json(status, "{\"error\":" + jsonQuote(name) + (reason.empty() ? "" : ",\"reason\":" + jsonQuote(reason)) + "}");
    if (code == ScheduleError::TooManyRequests) response->putHeader("Retry-After", "60");
    return response;
  }
};
#include OATPP_CODEGEN_END(ApiController)
} // namespace pcm::tokenbackend
