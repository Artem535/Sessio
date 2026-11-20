#pragma once

#include <optional>
#include <string>

namespace pcm::meeting {

enum class ProviderKind { ExternalUrl, LiveKit, Jitsi, ExternalJitsi };

[[nodiscard]] inline std::string providerKindToString(const ProviderKind kind) {
  switch (kind) {
  case ProviderKind::ExternalUrl:
    return "ExternalUrl";
  case ProviderKind::LiveKit:
    return "LiveKit";
  case ProviderKind::Jitsi:
    return "Jitsi";
  case ProviderKind::ExternalJitsi:
    return "ExternalJitsi";
  }
  return "ExternalUrl";
}

[[nodiscard]] inline std::optional<ProviderKind>
providerKindFromString(const std::string &value) {
  if (value == "ExternalUrl") {
    return ProviderKind::ExternalUrl;
  }
  if (value == "LiveKit") {
    return ProviderKind::LiveKit;
  }
  if (value == "Jitsi") {
    return ProviderKind::Jitsi;
  }
  if (value == "ExternalJitsi") return ProviderKind::ExternalJitsi;
  return std::nullopt;
}

// True for meetings created and cancelled through our token backend
// (LiveKit or Jitsi); ExternalUrl meetings are just a pasted link.
[[nodiscard]] inline bool isBackendHosted(const ProviderKind kind) {
  return kind == ProviderKind::LiveKit || kind == ProviderKind::Jitsi;
}

[[nodiscard]] inline bool isBackendHosted(const std::optional<ProviderKind> kind) {
  return kind.has_value() && isBackendHosted(*kind);
}

} // namespace pcm::meeting
