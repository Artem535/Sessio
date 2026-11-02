#include "crypto/jitsi_jwt.h"

#include "crypto/jwt_hs256.h"

#include <chrono>
#include <sstream>
#include <stdexcept>

namespace pcm::tokenbackend {

std::string mintJitsiJwt(const std::string &appId, const std::string &appSecret,
                         const std::string &room, const JitsiUser &user, int ttlSeconds,
                         std::optional<int64_t> nowUnixSeconds) {
  if (appId.empty() || appSecret.empty() || room.empty()) {
    throw std::invalid_argument("appId, appSecret, and room are required");
  }

  auto now = nowUnixSeconds.value_or(std::chrono::duration_cast<std::chrono::seconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count());

  std::ostringstream payload;
  payload << "{"
          << "\"aud\":\"jitsi\","
          << "\"iss\":\"" << jsonEscape(appId) << "\","
          << "\"sub\":\"*\","
          << "\"room\":\"" << jsonEscape(room) << "\","
          << "\"nbf\":" << (now - 10) << ","
          << "\"exp\":" << (now + ttlSeconds) << ","
          << "\"context\":{\"user\":{"
          << "\"id\":\"" << jsonEscape(user.id) << "\","
          << "\"name\":\"" << jsonEscape(user.name) << "\","
          << "\"moderator\":" << (user.moderator ? "true" : "false")
          << "}}}";

  return signHs256Jwt(payload.str(), appSecret);
}

} // namespace pcm::tokenbackend
