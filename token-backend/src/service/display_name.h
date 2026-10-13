#pragma once
#include <optional>
#include <string>
#include <string_view>
namespace pcm::tokenbackend {
std::optional<std::string> normaliseDisplayName(std::string_view utf8, bool required);
}
