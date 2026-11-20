#include "service/schedule_service.h"
#include "service/schedule_wire.h"
#include <stdexcept>
namespace pcm::tokenbackend {
bool ScheduleService::allowSync(AccountId account) {
  std::lock_guard guard(rateMutex_);
  auto now = std::chrono::steady_clock::now();
  auto &times = recentSync_[account];
  while (!times.empty() && times.front() <= now - std::chrono::seconds(60)) times.pop_front();
  if (times.size() >= 60) return false;
  times.push_back(now);
  return true;
}
bool ScheduleService::supportsScheduleSeries(const std::string &credential) { return authorizer_.authorize(credential).has_value(); }
ScheduleResult ScheduleService::get(const std::string &credential, const std::string &seriesUid) {
  auto account = authorizer_.authorize(credential);
  if (!account) return {{}, ScheduleError::Unauthorized, {}};
  if (!allowSync(*account)) return {{}, ScheduleError::TooManyRequests, {}};
  auto uid = normalizeSeriesUid(seriesUid);
  if (!uid) return {{}, ScheduleError::Invalid, "invalid_series_uid"};
  auto stored = repository_.get(*account, *uid);
  if (!stored) return {{}, ScheduleError::NotFound, {}};
  return {stored, {}, {}};
}
ScheduleResult ScheduleService::put(const std::string &credential, const std::string &seriesUid, const std::string &json) {
  auto account = authorizer_.authorize(credential);
  if (!account) return {{}, ScheduleError::Unauthorized, {}};
  if (!allowSync(*account)) return {{}, ScheduleError::TooManyRequests, {}};
  auto uid = normalizeSeriesUid(seriesUid);
  if (!uid) return {{}, ScheduleError::Invalid, "invalid_series_uid"};
  if (json.size() > kMaxScheduleBytes) return {{}, ScheduleError::TooLarge, "payload_too_large"};
  schedule::Snapshot s;
  try { s = parseScheduleJson(json); }
  catch (const std::invalid_argument &e) { return {{}, ScheduleError::Invalid, e.what()}; }
  if (!schedule::validate(s).valid) return {{}, ScheduleError::Invalid, "invalid_schedule_fields"};
  auto written = repository_.put(*account, *uid, s);
  switch (written.status) {
  case ScheduleWriteStatus::Applied: return {written.value, {}, {}};
  case ScheduleWriteStatus::Conflict: return {{}, ScheduleError::Conflict, {}};
  case ScheduleWriteStatus::NotFound: return {{}, ScheduleError::NotFound, {}};
  case ScheduleWriteStatus::Invalid: return {{}, ScheduleError::Invalid, "invalid_schedule_fields"};
  }
  return {{}, ScheduleError::Invalid, "invalid_schedule_fields"};
}
} // namespace pcm::tokenbackend
