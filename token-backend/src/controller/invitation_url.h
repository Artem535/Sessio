#pragma once

#include <string>

namespace pcm::tokenbackend {

std::string formatInvitationUrl(const std::string &templateOrPrefix, const std::string &code,
                                const std::string &passcode);

} // namespace pcm::tokenbackend
