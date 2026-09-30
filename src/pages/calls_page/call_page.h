#pragma once

#include "busy_spinner.h"
#include "device_check_widget.h"
#include "device_manager.h"
#include "remote_video_renderer.h"
#include "video_session.h"
#include "video_session_state.h"

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

// Hosts the remote-video widget stretched to fill the available area, with
// the local self-preview overlaid as a fixed-size tile in the bottom-right
// corner. A plain QWidget with no layout manager: QLayout has no way to
// express "fill entirely" and "float pinned to a corner" for two children
// at once, so both are positioned directly in resizeEvent().
class VideoStage final : public QWidget {
public:
  explicit VideoStage(QWidget *parent = nullptr);

  void setRemoteWidget(QWidget *widget);
  void setLocalPreviewWidget(QWidget *widget);

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  void layoutChildren();

  QPointer<QWidget> mRemoteWidget;
  QPointer<QWidget> mLocalPreviewWidget;
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
  void buildConnectedScreen();
  // Puts the attached session's provider's remoteVideoWidget() (or, if it
  // has none, mRemoteVideoPlaceholder) into mVideoStage's remote slot. The
  // provider's widget is only borrowed: it is reparented into mVideoStage
  // while shown, and handed back with setParent(nullptr) (never deleted)
  // when swapped out — see LiveKitVideoProvider's destructor for the other
  // half of this ownership contract.
  void updateRemoteVideoWidget();
  // Same borrowed-widget contract as updateRemoteVideoWidget(), but for the
  // attached provider's localVideoWidget() (self-preview), shown in
  // mVideoStage's picture-in-picture corner. Unlike the remote slot there is
  // no CallPage-owned placeholder: nullptr just means no preview is shown.
  void updateLocalPreviewWidget();
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
  // Text distinguishes VideoSessionState::Joining ("Connecting...") from
  // VideoSessionState::WaitingForClient ("Waiting for the other
  // participant..."), set in onSessionStateChanged() -- both states used to
  // show the same static "Connecting..." text, leaving a user who had
  // already joined with no way to tell that from still connecting.
  QLabel *mConnectingLabel{nullptr};
  QWidget *mConnectedView{nullptr};
  QHBoxLayout *mVideoRow{nullptr};
  pcm::video::detail::VideoStage *mVideoStage{nullptr};
  // CallPage-owned blank renderer, shown whenever the attached provider
  // offers no remote-video widget of its own (or before any session is
  // attached). Never reparented away from mVideoStage.
  QWidget *mRemoteVideoPlaceholder{nullptr};
  // Whichever widget currently occupies mVideoStage's remote slot.
  QPointer<QWidget> mActiveRemoteVideoWidget;
  // Whichever widget currently occupies mVideoStage's local-preview slot
  // (nullptr when the attached provider has none).
  QPointer<QWidget> mActiveLocalPreviewWidget;
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
  QPushButton *mNotesToggleButton{nullptr};
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
