#pragma once

#include "device_check_widget.h"
#include "device_manager.h"
#include "remote_video_renderer.h"
#include "video_session.h"
#include "video_session_state.h"

#include <QPointer>
#include <QWidget>

class QPushButton;
class QStackedWidget;

// Drives the visible call screen (device-check / connecting / connected /
// reconnecting / ended) off an attached pcm::video::VideoSession's state
// changes. Has no dependency on pcm::database::Database or ClientNotesPage
// — the side panel is any generic QWidget*, composed from outside — per the
// call-UI module boundary.
class CallPage final : public QWidget {
  Q_OBJECT

public:
  explicit CallPage(pcm::video::DeviceManager *deviceManager, QWidget *parent = nullptr);

  void attachSession(pcm::video::VideoSession *session); // does not take ownership
  void setSidePanelWidget(QWidget *panel);                // nullptr clears it; hidden until toggled
  void setSidePanelToggleVisible(bool visible);            // false in client mode: no toggle at all

signals:
  void leaveRequested();
  void callEnded();
  // Emitted when the user confirms their camera/mic/speaker choices on the
  // device-check screen and clicks its own Join button. CallPage has no
  // token client and no url/token of its own — it only relays "the user
  // confirmed" upward; CallsPage (which does hold the pending url/token from
  // its TokenBackendClient request) is the one that actually calls
  // VideoSession::join() in response. attachSession() must already have been
  // called by the time this fires, since the session driving this screen is
  // owned and constructed by CallsPage before the device-check screen is
  // ever shown.
  void joinConfirmed();

public slots:
  void onSessionStateChanged(pcm::video::VideoSessionState state);

private:
  void buildDeviceCheckScreen(pcm::video::DeviceManager *deviceManager);
  void buildConnectedScreen();

  QStackedWidget *mStack{nullptr};
  DeviceCheckWidget *mDeviceCheck{nullptr};
  QWidget *mConnectingScreen{nullptr};
  QWidget *mConnectedView{nullptr};
  QWidget *mReconnectingBanner{nullptr};
  QWidget *mEndedScreen{nullptr};
  QWidget *mSidePanelHost{nullptr};
  QPushButton *mNotesToggleButton{nullptr};
  QPointer<QWidget> mSidePanel;
  QPointer<pcm::video::VideoSession> mSession;
};
