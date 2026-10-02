#include "meeting_coordinator.h"

namespace pcm::meeting {

MeetingCoordinator::MeetingCoordinator(QString tokenBackendBaseUrl, QString bearerCredential,
                                       QObject *parent)
    : QObject(parent),
      mProviders{new ExternalUrlMeetingProvider(this),
                new LiveKitMeetingProvider(std::move(tokenBackendBaseUrl),
                                          std::move(bearerCredential), this)} {
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
  providerFor(kind)->create(request);
}

void MeetingCoordinator::cancelMeeting(const ProviderKind kind, const QString &meetingRef) {
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
