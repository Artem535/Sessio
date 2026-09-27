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
// CallPage -> session->join(url, token). This is the single widget both
// MainWindow and ClientModeWindow embed for calls; per the call-UI module
// boundary it has no dependency on pcm::database::Database or
// ClientNotesPage in this header.
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

signals:
  void eventKnownForCurrentCall(int64_t eventId);

private:
  void startJoin(const QString &url, const QString &token);

  pcm::video::DeviceManager *mDeviceManager;
  pcm::tokenclient::TokenBackendClient *mTokenClient;
  std::function<QString()> mBearerCredentialProvider;
  QStackedWidget *mStack{nullptr};
  CallEntryWidget *mEntryWidget{nullptr};
  CallPage *mCallPage{nullptr};
  std::unique_ptr<pcm::video::VideoSession> mSession;
  QList<UpcomingMeeting> mUpcomingMeetings;
  std::optional<int64_t> mCurrentEventId;
};
