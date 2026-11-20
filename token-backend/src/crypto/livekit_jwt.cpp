#include "crypto/livekit_jwt.h"

#include "crypto/jwt_hs256.h"

#include <chrono>
#include <sstream>
#include <stdexcept>

namespace pcm::tokenbackend {

std::string mintLiveKitJwt(const std::string &apiKey, const std::string &apiSecret,
                            const std::string &identity, const VideoGrants &grants,
                            int ttlSeconds, const std::string &metadata, const std::string &displayName,
                            std::optional<int64_t> nowUnixSeconds) {
  if (apiKey.empty() || apiSecret.empty() || identity.empty() || grants.room.empty()) {
    throw std::invalid_argument("apiKey, apiSecret, identity, and grants.room are required");
  }

  auto now = std::chrono::system_clock::now();
  auto nowSeconds = nowUnixSeconds.value_or(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
  auto expSeconds = nowSeconds + ttlSeconds;

  std::ostringstream payload;
  payload << "{"
          << "\"iss\":\"" << jsonEscape(apiKey) << "\","
          << "\"sub\":\"" << jsonEscape(identity) << "\","
          << "\"nbf\":" << nowSeconds << ","
          << "\"exp\":" << expSeconds << ",";
  if (!metadata.empty()) {
    payload << "\"metadata\":\"" << jsonEscape(metadata) << "\",";
  }
  if (!displayName.empty()) {
    payload << "\"name\":\"" << jsonEscape(displayName) << "\",";
  }
  payload << "\"video\":{"
          << "\"room\":\"" << jsonEscape(grants.room) << "\","
          << "\"roomJoin\":" << (grants.roomJoin ? "true" : "false") << ","
          << "\"canPublish\":" << (grants.canPublish ? "true" : "false") << ","
          << "\"canSubscribe\":" << (grants.canSubscribe ? "true" : "false") << ","
          << "\"canPublishData\":" << (grants.canPublishData ? "true" : "false")
          << "}"
          << "}";

  return signHs256Jwt(payload.str(), apiSecret);
}

} // namespace pcm::tokenbackend
