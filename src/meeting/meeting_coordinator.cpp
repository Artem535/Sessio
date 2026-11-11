#include "meeting_coordinator.h"
#include "jitsi_url.h"

namespace pcm::meeting {

MeetingCoordinator::MeetingCoordinator(const QString &tokenBackendBaseUrl, const QString &bearerCredential,
                                       QObject *parent)
    : QObject(parent),
      mProviders{new ExternalUrlMeetingProvider(this),
                new LiveKitMeetingProvider(tokenBackendBaseUrl, bearerCredential, this),
                new LiveKitMeetingProvider(tokenBackendBaseUrl, bearerCredential, this,
                                          QStringLiteral("jitsi"))} {
  for (auto *provider : mProviders) {
    connect(provider, &MeetingProvider::created, this, &MeetingCoordinator::meetingCreated);
    connect(provider, &MeetingProvider::createFailed, this,
            &MeetingCoordinator::meetingCreateFailed);
    connect(provider, &MeetingProvider::canceled, this, &MeetingCoordinator::meetingCanceled);
    connect(provider, &MeetingProvider::cancelFailed, this,
            &MeetingCoordinator::meetingCancelFailed);
  }
}

MeetingProvider *MeetingCoordinator::providerFor(const ProviderKind kind) const {
  return mProviders[static_cast<size_t>(kind)];
}

void MeetingCoordinator::createMeeting(const ProviderKind kind, const MeetingCreateRequest &request) {
  if (kind == ProviderKind::ExternalJitsi) {
    const auto server = parseJitsiServerUrl(request.rawMeetingUrl);
    if (!server) {
      emit meetingCreateFailed(tr("Enter a valid HTTPS Jitsi server address."));
      return;
    }
    MeetingDescriptor descriptor;
    descriptor.kind = kind;
    descriptor.meetingUrl = createJitsiRoomUrl(*server);
    emit meetingCreated(descriptor);
    return;
  }
  providerFor(kind)->create(request);
}

void MeetingCoordinator::cancelMeeting(const ProviderKind kind, const QString &meetingRef) {
  if (kind == ProviderKind::ExternalJitsi) { emit meetingCanceled(); return; }
  providerFor(kind)->cancel(meetingRef);
}

void MeetingCoordinator::setTokenBackendBaseUrl(const QString &baseUrl) {
  for (auto *provider : mProviders) {
    provider->setTokenBackendBaseUrl(baseUrl);
  }
}

void MeetingCoordinator::setBearerCredential(const QString &credential) {
  for (auto *provider : mProviders) {
    provider->setBearerCredential(credential);
  }
}

} // namespace pcm::meeting
