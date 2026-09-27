#pragma once

#include <cstdint>
#include <string>

namespace pcm::tokenbackend {

struct Config {
  uint16_t port;
  std::string dbPath;
  std::string liveKitApiKey;
  std::string liveKitApiSecret;
  // WebSocket URL handed back to clients. Has one correct real-world value for
  // this deployment, so it keeps a default.
  std::string liveKitWsEndpoint;
  // Prefix the invitation code is appended to. REQUIRED: a placeholder default
  // would silently hand out dead join links with no error anywhere.
  std::string invitationBaseUrl;
  int tokenTtlSeconds;

  // Throws std::runtime_error naming the variable if a required one is unset
  // or empty.
  static Config fromEnv();
};

} // namespace pcm::tokenbackend
