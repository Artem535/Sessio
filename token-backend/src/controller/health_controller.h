#pragma once

#include "oatpp/web/server/api/ApiController.hpp"
#include "oatpp/core/macro/codegen.hpp"
#include "oatpp/parser/json/mapping/ObjectMapper.hpp"

namespace pcm::tokenbackend {

#include OATPP_CODEGEN_BEGIN(ApiController)

class HealthController : public oatpp::web::server::api::ApiController {
public:
  HealthController()
      : oatpp::web::server::api::ApiController(
            oatpp::parser::json::mapping::ObjectMapper::createShared()) {}

  std::string statusText() const { return "ok"; }

  ENDPOINT("GET", "/healthz", getHealth) {
    return createResponse(Status::CODE_200, statusText());
  }
};

#include OATPP_CODEGEN_END(ApiController)

} // namespace pcm::tokenbackend
