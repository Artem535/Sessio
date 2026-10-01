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
}
