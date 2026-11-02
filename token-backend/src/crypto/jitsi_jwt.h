#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace pcm::tokenbackend {

struct JitsiUser {
  std::string id;
  std::string name;
  bool moderator = false;
};

// Mints a Jitsi (jitsi-meet JWT auth) token for one room. nbf is backdated by
// 10 s to tolerate clock skew between this backend and the Jitsi server.
std::string mintJitsiJwt(const std::string &appId, const std::string &appSecret,
                         const std::string &room, const JitsiUser &user, int ttlSeconds,
                         std::optional<int64_t> nowUnixSeconds = {});

} // namespace pcm::tokenbackend
