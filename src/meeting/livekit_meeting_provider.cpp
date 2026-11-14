#include "livekit_meeting_provider.h"

namespace pcm::meeting {

LiveKitMeetingProvider::LiveKitMeetingProvider(QString tokenBackendBaseUrl,
                                               QString bearerCredential, QObject *parent,
                                               QString backendProvider)
    : MeetingProvider(parent),
      mClient(std::make_unique<pcm::tokenclient::TokenBackendClient>(std::move(tokenBackendBaseUrl))),
      mBearerCredential(std::move(bearerCredential)),
      mBackendProvider(std::move(backendProvider)) {
  const auto kind = mBackendProvider == QStringLiteral("jitsi") ? ProviderKind::Jitsi
                                                                : ProviderKind::LiveKit;
  connect(mClient.get(), &pcm::tokenclient::TokenBackendClient::meetingCreated, this,
          [this, kind](const pcm::tokenclient::MeetingCreateResult &result) {
            MeetingDescriptor descriptor;
            descriptor.kind = kind;
            descriptor.meetingRef = result.meetingRef;
            descriptor.meetingUrl = result.invitationUrl;
            descriptor.invitationState = result.invitationUrl + "|" + result.passcode;
            emit created(descriptor);
          });
  connect(mClient.get(), &pcm::tokenclient::TokenBackendClient::meetingCreateFailed, this,
          &MeetingProvider::createFailed);
  connect(mClient.get(), &pcm::tokenclient::TokenBackendClient::meetingInvalidated, this,
          &MeetingProvider::canceled);
  connect(mClient.get(), &pcm::tokenclient::TokenBackendClient::meetingInvalidateFailed, this,
          &MeetingProvider::cancelFailed);
}

void LiveKitMeetingProvider::create(const MeetingCreateRequest &request) {
  mClient->requestCreateMeeting(mBearerCredential, request.scheduledStartIso, request.scheduledEndIso,
                            mBackendProvider);
}

void LiveKitMeetingProvider::cancel(const QString &meetingRef) {
  mClient->requestInvalidateMeeting(mBearerCredential, meetingRef);
}

void LiveKitMeetingProvider::setTokenBackendBaseUrl(const QString &baseUrl) {
  mClient->setBaseUrl(baseUrl);
}

void LiveKitMeetingProvider::setBearerCredential(const QString &credential) {
  mBearerCredential = credential;
}

} // namespace pcm::meeting
