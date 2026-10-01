#pragma once

#include "busy_spinner.h"
#include "device_check_widget.h"
#include "device_manager.h"
#include "participant_tile.h"
#include "video_session.h"
#include "video_session_state.h"

#include <QGridLayout>
#include <QHash>
#include <QPointer>
#include <QResizeEvent>
#include <QWidget>

class QComboBox;
class QHBoxLayout;
class QLabel;
class QMenu;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace pcm::video::detail {

// UI-owned participant grid with independent overlay controls.
class VideoStage final : public QWidget {
public:
  explicit VideoStage(QWidget *parent = nullptr);

  void setTiles(const QVector<ParticipantTile *> &tiles);
  void setControlBarWidget(QWidget *widget);
  void setNotesToggleWidget(QWidget *widget);

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  void layoutChildren();

  QWidget *mTileHost;
  QGridLayout *mTileGrid;
  QVector<ParticipantTile *> mTiles;
  QPointer<QWidget> mControlBarWidget;
  QPointer<QWidget> mNotesToggleWidget;
};

} // namespace pcm::video::detail

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
  // Expands (or collapses) the notes side panel immediately if the toggle
  // button already exists (setSidePanelToggleVisible(true) was already
  // called); a no-op otherwise. Called by CallsPage once it knows whether
  // the current call is linked to a real client/event.
  void setSidePanelExpandedByDefault(bool expanded);

signals:
  void leaveRequested();
  void deviceCheckCanceled();
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
  // Passes the device-check screen's camera/microphone/speaker choice to the provider.
  void applyDeviceCheckSelection();
  void buildConnectedScreen();
  void syncParticipants();
  void clearParticipants();
  void refreshMediaButtons();
  void onSessionFailureReason(const QString &reason);
  void refreshEndedReason();
  // Shows mMediaErrorBanner with `reason` and starts its auto-hide timer.
  // Never touches mLastFailureReason/mEndedReasonLabel/mStack — a
  // mediaError() is a non-fatal, transient local-device notice, not a call
  // end (see VideoProvider::mediaError()'s doc comment).
  void onMediaError(const QString &reason);
  // Builds and pops up the camera/microphone/speaker selector menu anchored
  // below mDevicesButton. Rebuilt from scratch on every click (the menu sets
  // Qt::WA_DeleteOnClose) so it always reflects the current device list and
  // the currently attached session's provider.
  void showDevicesPopover();

  QStackedWidget *mStack{nullptr};
  DeviceCheckWidget *mDeviceCheck{nullptr};
  QWidget *mConnectingScreen{nullptr};
  QLabel *mConnectingLabel{nullptr};
  QLabel *mWaitingLabel{nullptr};
  QWidget *mConnectedView{nullptr};
  QHBoxLayout *mVideoRow{nullptr};
  pcm::video::detail::VideoStage *mVideoStage{nullptr};
  // Floating, opaque overlay carrying the mic/camera/devices/fullscreen/leave
  // buttons. Owned by VideoStage once handed to setControlBarWidget() --
  // VideoStage reparents, positions, and shows/hides it internally.
  QWidget *mControlBar{nullptr};
  QHash<QString, pcm::video::ParticipantTile *> mTiles;
  QVector<QMetaObject::Connection> mParticipantConnections;
  QWidget *mReconnectingBanner{nullptr};
  QLabel *mReconnectingLabel{nullptr};
  // Non-fatal, transient local-device notice (VideoSession::mediaError()) —
  // distinct from mReconnectingBanner (connection state) and mEndedReasonLabel
  // (a terminal reason on the ended screen): the call keeps running while
  // this is shown. Lives on the connected screen, hidden by default, shown
  // by onMediaError() and auto-hidden a few seconds later by a QTimer.
  QLabel *mMediaErrorBanner{nullptr};
  QWidget *mEndedScreen{nullptr};
  QLabel *mEndedReasonLabel{nullptr};
  QWidget *mSidePanelHost{nullptr};
  QToolButton *mNotesToggleButton{nullptr};
  QToolButton *mMicrophoneToggleButton{nullptr};
  QToolButton *mCameraToggleButton{nullptr};
  QToolButton *mFullscreenToggleButton{nullptr};
  QToolButton *mDevicesButton{nullptr};
  // Owned by whichever showDevicesPopover() call last ran; the popover menu
  // deletes itself (Qt::WA_DeleteOnClose) on close, at which point these
  // dangle deliberately until the next click rebuilds them.
  QComboBox *mDeviceCameraCombo{nullptr};
  QComboBox *mDeviceMicrophoneCombo{nullptr};
  QComboBox *mDeviceSpeakerCombo{nullptr};
  QPointer<QWidget> mSidePanel;
  QPointer<pcm::video::VideoSession> mSession;
  // Not owned; passed into the constructor and outlives this CallPage (see
  // CallsPage, which owns the DeviceManager). Used by showDevicesPopover()
  // to enumerate the current camera/microphone/speaker lists.
  pcm::video::DeviceManager *mDeviceManager{nullptr};
  // The reason from the attached session's most recent joinFailed()/
  // reconnectFailed(), shown on the ended screen. Reset per session.
  QString mLastFailureReason;
};
