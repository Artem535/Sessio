#pragma once
#include "service/meeting_service.h"
#include "db/schedule_repository.h"
#include <functional>
#include <memory>

namespace pcm::tokenbackend {
// Seconds since Unix epoch. Production always defaults to system_clock.
using ServiceClock = std::function<int64_t()>;
class SeriesService {
public:
  struct Invitation {
    std::string code;
    std::string passcode;
    int64_t generation;
    std::string invitationUrl;
  };
  SeriesService(SqliteConnection &conn, Authorizer &authorizer,
                MeetingsRepository &meetings, const Config &config,
                std::string endpoint, ServiceClock clock = {});
  ~SeriesService();
  Result<Invitation> invitation(const std::string &credential, const std::string &uid,
                                const std::string &key, const std::string &json);
  Result<std::monostate> revoke(const std::string &credential, const std::string &uid);
  Result<TokenResult> specialistToken(const std::string &credential, const std::string &uid,
                                      int64_t originalStartMs, const std::string &name = {});
  // nullopt means a legacy code/ref; a recognized series resource always returns a result.
  std::optional<Result<TokenResult>> clientToken(const std::string &code, const std::string &passcode,
                                                const std::string &name = {});
  std::optional<Result<TokenResult>> mappedToken(const Meeting &meeting, bool client,
                                                const std::string &name = {});
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace pcm::tokenbackend
