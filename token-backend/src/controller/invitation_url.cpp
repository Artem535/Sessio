#include "controller/invitation_url.h"

#include <format>

namespace pcm::tokenbackend {
namespace {
std::string encode(const std::string &value) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string result;
  for (unsigned char c : value) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') result += c;
    else { result += '%'; result += hex[c >> 4]; result += hex[c & 15]; }
  }
  return result;
}
}

std::string formatInvitationUrl(const std::string &templateOrPrefix, const std::string &code,
                                const std::string &passcode) {
  if (templateOrPrefix.find('{') == std::string::npos) {
    return templateOrPrefix + encode(code);
  }
  auto encodedCode = encode(code);
  auto encodedPasscode = encode(passcode);
  return std::vformat(templateOrPrefix, std::make_format_args(encodedCode, encodedPasscode));
}

} // namespace pcm::tokenbackend
