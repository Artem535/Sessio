#pragma once

#include <string>

namespace pcm::tokenbackend {

struct VideoGrants {
  std::string room;
  bool roomJoin = true;
  bool canPublish = true;
  bool canSubscribe = true;
  bool canPublishData = true;
};

std::string mintLiveKitJwt(const std::string &apiKey, const std::string &apiSecret,
                            const std::string &identity, const VideoGrants &grants,
                            int ttlSeconds, const std::string &metadata = {},
                            const std::string &displayName = {});

} // namespace pcm::tokenbackend
