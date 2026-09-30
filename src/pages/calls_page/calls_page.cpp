#include "calls_page.h"
#include "livekit_video_provider.h"

#include <QStackedWidget>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <utility>

namespace {

std::pair<QString, QString> joinCredentialsFromInput(const QString &codeOrLink,
                                                      const QString &passcode) {
  const QUrl url(codeOrLink.trimmed());
  if (url.isValid() && url.scheme() == QStringLiteral("sessio") &&
      url.host() == QStringLiteral("join")) {
    const QUrlQuery query(url);
    const QString invitationCode = query.queryItemValue(QStringLiteral("code"));
    const QString invitationPasscode = query.queryItemValue(QStringLiteral("passcode"));
    if (!invitationCode.isEmpty() && !invitationPasscode.isEmpty()) {
      return {invitationCode, invitationPasscode};
    }
  }
  return {codeOrLink, passcode};
}

} // namespace

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

  // Connected exactly once, here, for CallsPage's whole lifetime — both join
  // paths below share it. It used to be (re-)made inside each per-click
  // handler with Qt::UniqueConnection, which Qt cannot honour for a lambda
  // (uniqueness is only detectable for pointer-to-member-function slots): a
  // Debug build asserts on the first click, and a release build rejects the
  // connection outright on Qt 6.10 (so no token was ever acted on) or, on
  // older Qt, silently stacks one more duplicate per click (so the Nth token
  // started N joins).
  connect(mTokenClient, &pcm::tokenclient::TokenBackendClient::tokenReceived, this,
          [this](const pcm::tokenclient::TokenResult &result) {
            startJoin(result.endpointUrl, result.token);
          });
  connect(mTokenClient, &pcm::tokenclient::TokenBackendClient::tokenRequestFailed, mEntryWidget,
          &CallEntryWidget::showError);

  connect(mEntryWidget, &CallEntryWidget::joinByCodeRequested, this,
          [this](const QString &code, const QString &passcode) {
            mCurrentEventId.reset(); // a code/passcode join never has a known Event
            mEntryWidget->clearError();
            const auto [invitationCode, invitationPasscode] =
                joinCredentialsFromInput(code, passcode);
            mTokenClient->requestClientToken(invitationCode, invitationPasscode);
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
            mEntryWidget->clearError();
            mTokenClient->requestSpecialistToken(mBearerCredentialProvider(), meetingRef);
          });

  connect(mCallPage, &CallPage::callEnded, this, [this]() { mStack->setCurrentWidget(mEntryWidget); });
  connect(mCallPage, &CallPage::deviceCheckCanceled, this, [this]() {
    if (!mSession) {
      return;
    }
    using pcm::video::VideoSessionState;
    const auto state = mSession->state();
    if (state != VideoSessionState::NoMeeting && state != VideoSessionState::Provisioned &&
        state != VideoSessionState::PrejoinCheck) {
      return;
    }
    mSession.reset();
    mPendingUrl.clear();
    mPendingToken.clear();
    mCurrentEventId.reset();
    mStack->setCurrentWidget(mEntryWidget);
  });

  // Fix round 1: the real join() call is gated behind the user's own
  // confirmation on CallPage's device-check screen, not fired the instant a
  // token arrives (see startJoin() below). mCallPage is a single instance
  // constructed once above and living for CallsPage's whole lifetime, so a
  // single constructor-time connection is sufficient here (the same holds
  // for the tokenReceived connection above).
  connect(mCallPage, &CallPage::joinConfirmed, this, [this]() {
    if (mSession) {
      mSession->join(mPendingUrl, mPendingToken);
    }
  });

  // The Leave button on CallPage's connected screen.
  connect(mCallPage, &CallPage::leaveRequested, this, [this]() {
    if (mSession) {
      mSession->leave();
    }
  });
}

bool CallsPage::hasActiveCall() const {
  if (!mSession) {
    return false;
  }
  using pcm::video::VideoSessionState;
  const auto state = mSession->state();
  return state != VideoSessionState::NoMeeting && state != VideoSessionState::Ended &&
         state != VideoSessionState::Failed;
}

void CallsPage::setBearerCredentialProvider(std::function<QString()> provider) {
  mBearerCredentialProvider = std::move(provider);
}

void CallsPage::setUpcomingMeetings(const QList<UpcomingMeeting> &meetings) {
  mUpcomingMeetings = meetings;
  mEntryWidget->setUpcomingMeetings(meetings);
}

void CallsPage::preselectOwnMeeting(const QString &meetingRef) {
  // An "Open Meeting" click arriving mid-call must not switch away from
  // (and so hide) the live call.
  if (hasActiveCall()) {
    return;
  }
  mStack->setCurrentWidget(mEntryWidget);
  mEntryWidget->preselectOwnMeeting(meetingRef);
}

void CallsPage::prefillJoinCode(const QString &code, const QString &passcode) {
  // Same for a forwarded sessio:// link arriving mid-call.
  if (hasActiveCall()) {
    return;
  }
  mStack->setCurrentWidget(mEntryWidget);
  mEntryWidget->prefillJoinCode(code, passcode);
}

void CallsPage::setSidePanelWidget(QWidget *panel) { mCallPage->setSidePanelWidget(panel); }

void CallsPage::setSidePanelExpandedByDefault(bool expanded) { mCallPage->setSidePanelExpandedByDefault(expanded); }

void CallsPage::setVideoProviderFactoryForTesting(std::function<pcm::video::VideoProvider *()> factory) {
  mVideoProviderFactory = std::move(factory);
}

void CallsPage::startJoin(const QString &url, const QString &token) {
  // Bug 2 (fixwave group 5): a rapid double-click, or a stale/delayed token
  // response arriving after a call is already under way, must not destroy
  // the in-progress VideoSession/provider out from under it. preselectOwnMeeting()/
  // prefillJoinCode() already guard the same way.
  if (hasActiveCall()) {
    return;
  }
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
  // CallPage shows the failure reason on its ended screen, but callEnded
  // switches this page straight back to the entry form (see the
  // constructor), so surface it there too. Both connections die with the
  // session.
  connect(mSession.get(), &pcm::video::VideoSession::joinFailed, mEntryWidget,
          &CallEntryWidget::showError);
  connect(mSession.get(), &pcm::video::VideoSession::reconnectFailed, mEntryWidget,
          &CallEntryWidget::showError);
  // Bug 1 (fixwave group 5): connectionLost() is the most common real-world
  // failure (a live call dropping mid-call), and this is the connection
  // that's actually visible — CallPage's own ended screen shows the same
  // reason, but callEnded() immediately switches this page back to the entry
  // form (see the constructor), hiding it.
  connect(mSession.get(), &pcm::video::VideoSession::connectionLost, mEntryWidget,
          &CallEntryWidget::showError);
  mCallPage->attachSession(mSession.get());
  mStack->setCurrentWidget(mCallPage);
  if (mCurrentEventId.has_value()) {
    emit eventKnownForCurrentCall(*mCurrentEventId);
  }
}
