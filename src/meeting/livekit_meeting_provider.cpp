#include "livekit_meeting_provider.h"

namespace pcm::meeting {

LiveKitMeetingProvider::LiveKitMeetingProvider(QString tokenBackendBaseUrl,
                                               QString bearerCredential, QObject *parent)
    : MeetingProvider(parent),
      mClient(std::make_unique<pcm::tokenclient::TokenBackendClient>(std::move(tokenBackendBaseUrl))),
      mBearerCredential(std::move(bearerCredential)) {
  connect(mClient.get(), &pcm::tokenclient::TokenBackendClient::meetingCreated, this,
          [this](const pcm::tokenclient::MeetingCreateResult &result) {
            MeetingDescriptor descriptor;
            descriptor.kind = ProviderKind::LiveKit;
            descriptor.meetingRef = result.meetingRef;
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
  mClient->requestCreateMeeting(mBearerCredential, request.scheduledStartIso, request.scheduledEndIso);
}

void LiveKitMeetingProvider::cancel(const QString &meetingRef) {
  mClient->requestInvalidateMeeting(mBearerCredential, meetingRef);
}

} // namespace pcm::meeting
