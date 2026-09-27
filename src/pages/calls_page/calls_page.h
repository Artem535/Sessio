#pragma once

#include "call_entry_widget.h"
#include "call_page.h"
#include "device_manager.h"
#include "token_backend_client.h"
#include "video_session.h"

#include <QList>
#include <QString>
#include <QWidget>
#include <functional>
#include <memory>
#include <optional>

class QStackedWidget;

// Assembles CallEntryWidget (the "pick a meeting / join by code" screen) and
// CallPage (the device-check / connected / ended screens) into a
// QStackedWidget, and drives the real join sequence: the user requests a
// join (own meeting or code+passcode) -> TokenBackendClient fetches a token
// -> a LiveKitVideoProvider+VideoSession pair is constructed -> attached to
// CallPage, which shows its device-check screen -> the user reviews their
// camera/mic/speaker and clicks Join there (CallPage::joinConfirmed) ->
// session->join(url, token) is finally called. The device-check screen is a
// real confirmation gate, not a pass-through: the url/token are held pending
// from the moment the token arrives until the user actually confirms. This
// is the single widget both MainWindow and ClientModeWindow embed for calls;
// per the call-UI module boundary it has no dependency on
// pcm::database::Database or ClientNotesPage in this header.
class CallsPage final : public QWidget {
  Q_OBJECT

public:
  CallsPage(bool specialistMode, pcm::video::DeviceManager *deviceManager,
            pcm::tokenclient::TokenBackendClient *tokenClient, QWidget *parent = nullptr);

  void setBearerCredentialProvider(std::function<QString()> provider); // specialist mode only
  void setUpcomingMeetings(const QList<UpcomingMeeting> &meetings);
  void preselectOwnMeeting(const QString &meetingRef);
  void prefillJoinCode(const QString &code, const QString &passcode);
  void setSidePanelWidget(QWidget *panel);

  // Testing seam only: overrides the VideoProvider constructed for
  // subsequent joins. Production callers (MainWindow/ClientModeWindow) never
  // call this and get the default real pcm::video::LiveKitVideoProvider;
  // test code substitutes a FakeVideoProvider so the device-check join gate
  // (fix round 1: session->join() only fires once CallPage::joinConfirmed
  // arrives, not the instant a token arrives) can be verified deterministically,
  // without a real LiveKit SDK/network connection.
  void setVideoProviderFactoryForTesting(std::function<pcm::video::VideoProvider *()> factory);

signals:
  void eventKnownForCurrentCall(int64_t eventId);

private:
  // Constructs the VideoSession/provider and shows CallPage once a token
  // arrives, but (fix round 1) no longer calls session->join() itself —
  // that only happens once the user confirms on the device-check screen
  // (CallPage::joinConfirmed), via the constructor-time connection below.
  void startJoin(const QString &url, const QString &token);

  pcm::video::DeviceManager *mDeviceManager;
  pcm::tokenclient::TokenBackendClient *mTokenClient;
  std::function<QString()> mBearerCredentialProvider;
  std::function<pcm::video::VideoProvider *()> mVideoProviderFactory;
  QStackedWidget *mStack{nullptr};
  CallEntryWidget *mEntryWidget{nullptr};
  CallPage *mCallPage{nullptr};
  std::unique_ptr<pcm::video::VideoSession> mSession;
  QList<UpcomingMeeting> mUpcomingMeetings;
  std::optional<int64_t> mCurrentEventId;
  // The url/token from the most recent tokenReceived() are held here rather
  // than acted on immediately: they're only used once the user confirms on
  // CallPage's device-check screen (see startJoin()/the joinConfirmed
  // connection in the constructor).
  QString mPendingUrl;
  QString mPendingToken;
};
