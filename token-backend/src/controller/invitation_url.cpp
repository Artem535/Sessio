#include "controller/invitation_url.h"

#include <format>

namespace pcm::tokenbackend {

std::string formatInvitationUrl(const std::string &templateOrPrefix, const std::string &code,
                                const std::string &passcode) {
  if (templateOrPrefix.find('{') == std::string::npos) {
    return templateOrPrefix + code;
  }
  return std::vformat(templateOrPrefix, std::make_format_args(code, passcode));
}

} // namespace pcm::tokenbackend
