#pragma once
#include "schedule/schedule.h"
#include <optional>
#include <string>
namespace pcm::tokenbackend {
inline constexpr size_t kMaxScheduleBytes = 1024 * 1024;
// Throws invalid_argument with a static, non-sensitive reason on invalid input.
schedule::Snapshot parseScheduleJson(const std::string &json);
std::string scheduleJson(const schedule::Snapshot &snapshot);
std::string jsonQuote(const std::string &value);
bool parseSeriesInvitationJson(const std::string &json);
std::string parseSpecialistNameJson(const std::string &json);
int64_t parseUtcTimestamp(const std::string &value);
std::string utcTimestamp(int64_t ms);
}
