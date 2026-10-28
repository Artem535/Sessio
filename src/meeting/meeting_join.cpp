#include "meeting_join.h"

#include "provider_kind.h"

namespace pcm::meeting {

JoinAction resolveJoinAction(const DuckEvent &event, const std::optional<QString> &seriesJoinTarget) {
  if (seriesJoinTarget && !seriesJoinTarget->isEmpty()) {
    return {JoinAction::Kind::LiveKit, *seriesJoinTarget};
  }
  if (event.provider_kind && event.meeting_ref && !event.meeting_ref->empty() &&
      providerKindFromString(*event.provider_kind) == ProviderKind::LiveKit) {
    return {JoinAction::Kind::LiveKit, QString::fromStdString(*event.meeting_ref)};
  }
  if (!event.meeting_url.empty()) {
    return {JoinAction::Kind::Url, QString::fromStdString(event.meeting_url)};
  }
  return {};
}

} // namespace pcm::meeting
