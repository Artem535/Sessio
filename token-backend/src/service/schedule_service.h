#pragma once
#include "auth/authorizer.h"
#include "db/schedule_repository.h"
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <deque>

namespace pcm::tokenbackend {
enum class ScheduleError { Unauthorized, NotFound, Invalid, TooLarge, Conflict, TooManyRequests };
struct ScheduleResult {
  std::optional<StoredSchedule> value;
  std::optional<ScheduleError> error;
  std::string reason;
  bool ok() const { return value.has_value(); }
};
class ScheduleService {
public:
  ScheduleService(Authorizer &authorizer, ScheduleRepository &repository)
      : authorizer_(authorizer), repository_(repository) {}
  bool supportsScheduleSeries(const std::string &credential);
  ScheduleResult get(const std::string &credential, const std::string &seriesUid);
  ScheduleResult put(const std::string &credential, const std::string &seriesUid,
                     const std::string &rawJson);
private:
  Authorizer &authorizer_;
  ScheduleRepository &repository_;
  bool allowSync(AccountId account);
  std::mutex rateMutex_;
  std::unordered_map<AccountId, std::deque<std::chrono::steady_clock::time_point>> recentSync_;
};
} // namespace pcm::tokenbackend
