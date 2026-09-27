#include "calls_page.h"
#include "livekit_video_provider.h"

#include <QStackedWidget>
#include <QVBoxLayout>

CallsPage::CallsPage(const bool specialistMode, pcm::video::DeviceManager *deviceManager,
                     pcm::tokenclient::TokenBackendClient *tokenClient, QWidget *parent)
    : QWidget(parent), mDeviceManager(deviceManager), mTokenClient(tokenClient),
      mVideoProviderFactory([]() -> pcm::video::VideoProvider * {
        return new pcm::video::LiveKitVideoProvider();
      }) {
  auto *layout = new QVBoxLayout(this);
  mStack = new QStackedWidget(this);
  layout->addWidget(mStack);

  mEntryWidget = new CallEntryWidget(specialistMode, this);
  mCallPage = new CallPage(mDeviceManager, this);
  mCallPage->setSidePanelToggleVisible(specialistMode);
  mStack->addWidget(mEntryWidget);
  mStack->addWidget(mCallPage);
  mStack->setCurrentWidget(mEntryWidget);

  connect(mEntryWidget, &CallEntryWidget::joinByCodeRequested, this,
          [this](const QString &code, const QString &passcode) {
            mCurrentEventId.reset(); // a code/passcode join never has a known Event
            connect(mTokenClient, &pcm::tokenclient::TokenBackendClient::tokenReceived, this,
                    [this](const pcm::tokenclient::TokenResult &result) {
                      startJoin(result.endpointUrl, result.token);
                    },
                    Qt::UniqueConnection);
            mTokenClient->requestClientToken(code, passcode);
          });

  connect(mEntryWidget, &CallEntryWidget::ownMeetingJoinRequested, this,
          [this](const QString &meetingRef) {
            if (!mBearerCredentialProvider) {
              return;
            }
            mCurrentEventId.reset();
            for (const auto &meeting : mUpcomingMeetings) {
              if (meeting.meetingRef == meetingRef) {
                mCurrentEventId = meeting.eventId;
                break;
              }
            }
            connect(mTokenClient, &pcm::tokenclient::TokenBackendClient::tokenReceived, this,
                    [this](const pcm::tokenclient::TokenResult &result) {
                      startJoin(result.endpointUrl, result.token);
                    },
                    Qt::UniqueConnection);
            mTokenClient->requestSpecialistToken(mBearerCredentialProvider(), meetingRef);
          });

  connect(mCallPage, &CallPage::callEnded, this, [this]() { mStack->setCurrentWidget(mEntryWidget); });

  // Fix round 1: the real join() call is gated behind the user's own
  // confirmation on CallPage's device-check screen, not fired the instant a
  // token arrives (see startJoin() below). mCallPage is a single instance
  // constructed once above and living for CallsPage's whole lifetime, so a
  // single constructor-time connection is sufficient here — unlike the
  // tokenReceived connections above, which are re-established per join
  // attempt against the shared mTokenClient and so need Qt::UniqueConnection
  // to avoid stacking duplicate connections across repeated join attempts.
  connect(mCallPage, &CallPage::joinConfirmed, this, [this]() {
    if (mSession) {
      mSession->join(mPendingUrl, mPendingToken);
    }
  });
}

void CallsPage::setBearerCredentialProvider(std::function<QString()> provider) {
  mBearerCredentialProvider = std::move(provider);
}

void CallsPage::setUpcomingMeetings(const QList<UpcomingMeeting> &meetings) {
  mUpcomingMeetings = meetings;
  mEntryWidget->setUpcomingMeetings(meetings);
}

void CallsPage::preselectOwnMeeting(const QString &meetingRef) {
  mStack->setCurrentWidget(mEntryWidget);
  mEntryWidget->preselectOwnMeeting(meetingRef);
}

void CallsPage::prefillJoinCode(const QString &code, const QString &passcode) {
  mStack->setCurrentWidget(mEntryWidget);
  mEntryWidget->prefillJoinCode(code, passcode);
}

void CallsPage::setSidePanelWidget(QWidget *panel) { mCallPage->setSidePanelWidget(panel); }

void CallsPage::setVideoProviderFactoryForTesting(std::function<pcm::video::VideoProvider *()> factory) {
  mVideoProviderFactory = std::move(factory);
}

void CallsPage::startJoin(const QString &url, const QString &token) {
  // Fix round 1: the url/token are stashed for later rather than joined
  // immediately — the actual VideoSession::join() call now happens only once
  // the user confirms on CallPage's device-check screen (see the
  // CallPage::joinConfirmed connection in the constructor), matching the
  // design spec's PrejoinCheck gate and Task 12's original note that this is
  // where the real join flow was meant to be triggered from.
  mPendingUrl = url;
  mPendingToken = token;
  auto *provider = mVideoProviderFactory();
  mSession = std::make_unique<pcm::video::VideoSession>(provider);
  mCallPage->attachSession(mSession.get());
  mStack->setCurrentWidget(mCallPage);
  if (mCurrentEventId.has_value()) {
    emit eventKnownForCurrentCall(*mCurrentEventId);
  }
}
