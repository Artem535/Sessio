#include "calls_page.h"
#include "livekit_video_provider.h"

#include <QStackedWidget>
#include <QVBoxLayout>

CallsPage::CallsPage(const bool specialistMode, pcm::video::DeviceManager *deviceManager,
                     pcm::tokenclient::TokenBackendClient *tokenClient, QWidget *parent)
    : QWidget(parent), mDeviceManager(deviceManager), mTokenClient(tokenClient) {
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

void CallsPage::startJoin(const QString &url, const QString &token) {
  auto *provider = new pcm::video::LiveKitVideoProvider();
  mSession = std::make_unique<pcm::video::VideoSession>(provider);
  mCallPage->attachSession(mSession.get());
  mStack->setCurrentWidget(mCallPage);
  mSession->join(url, token);
  if (mCurrentEventId.has_value()) {
    emit eventKnownForCurrentCall(*mCurrentEventId);
  }
}
