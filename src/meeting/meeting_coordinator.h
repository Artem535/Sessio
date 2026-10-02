#pragma once

#include <array>

#include "external_url_meeting_provider.h"
#include "livekit_meeting_provider.h"
#include "meeting_provider.h"

namespace pcm::meeting {

// Selects the right MeetingProvider by ProviderKind and drives it on behalf of
// the event-editing UI and the timeline model. This is the only class either
// of those touches directly — neither has to know ExternalUrlMeetingProvider
// or LiveKitMeetingProvider exist.
class MeetingCoordinator final : public QObject {
  Q_OBJECT

public:
  MeetingCoordinator(QString tokenBackendBaseUrl, QString bearerCredential,
                     QObject *parent = nullptr);

  void createMeeting(ProviderKind kind, const MeetingCreateRequest &request);
  void cancelMeeting(ProviderKind kind, const QString &meetingRef);

  // Live settings propagation: applies to every provider that cares (today,
  // only LiveKitMeetingProvider — ExternalUrlMeetingProvider's override is a
  // no-op), so the event editor's LiveKit meeting create/cancel flow picks up
  // a Settings-dialog change without an app restart.
  void setTokenBackendBaseUrl(const QString &baseUrl);
  void setBearerCredential(const QString &credential);

signals:
  void meetingCreated(pcm::meeting::MeetingDescriptor descriptor);
  void meetingCreateFailed(QString error);
  void meetingCanceled();
  void meetingCancelFailed(QString error);

private:
  [[nodiscard]] MeetingProvider *providerFor(ProviderKind kind) const;

  // Indexed by static_cast<size_t>(ProviderKind) — avoids a second switch
  // over ProviderKind alongside providerKindToString's.
  std::array<MeetingProvider *, 2> mProviders;
};

} // namespace pcm::meeting
