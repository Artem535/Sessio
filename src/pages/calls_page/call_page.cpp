#include "call_page.h"

#include "call_control_icons.h"

#include <QAudioDevice>
#include <QCameraDevice>
#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QMenu>
#include <QPoint>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace {
// How long CallPage's transient media-error banner stays visible before
// auto-hiding. A device error (camera/mic/publish failure) is a one-off
// notice, not a persistent condition the UI needs to keep nagging about —
// but long enough for a user who glances away for a moment to still catch
// it. Chosen within the 5-8s range suggested for this kind of transient,
// non-fatal notice; not user-configurable since there's no evidence a fixed
// value here needs to be.
constexpr int kMediaErrorBannerAutoHideMs = 6000;
} // namespace

namespace pcm::video::detail {

namespace {
constexpr int kLocalPreviewWidth = 160;
constexpr int kLocalPreviewHeight = 90;
constexpr int kLocalPreviewMargin = 12;
} // namespace

VideoStage::VideoStage(QWidget *parent) : QWidget(parent) {}

void VideoStage::setRemoteWidget(QWidget *widget) {
  mRemoteWidget = widget;
  layoutChildren();
}

void VideoStage::setLocalPreviewWidget(QWidget *widget) {
  mLocalPreviewWidget = widget;
  layoutChildren();
}

// Unlike setRemoteWidget()/setLocalPreviewWidget() above (whose callers in
// CallPage reparent the borrowed provider widget themselves before handing
// it over), the control-bar and notes-toggle widgets set here are owned and
// constructed by CallPage/CallsPage as plain child widgets with no separate
// reparenting step, so VideoStage does that reparenting itself and hands a
// previous widget back unparented (rather than deleting it) when swapped.
void VideoStage::setControlBarWidget(QWidget *widget) {
  if (mControlBarWidget == widget) {
    return;
  }
  if (mControlBarWidget) {
    mControlBarWidget->setParent(nullptr);
  }
  mControlBarWidget = widget;
  if (mControlBarWidget) {
    mControlBarWidget->setParent(this);
    mControlBarWidget->show();
  }
  layoutChildren();
}

void VideoStage::setNotesToggleWidget(QWidget *widget) {
  if (mNotesToggleWidget == widget) {
    return;
  }
  if (mNotesToggleWidget) {
    mNotesToggleWidget->setParent(nullptr);
  }
  mNotesToggleWidget = widget;
  if (mNotesToggleWidget) {
    mNotesToggleWidget->setParent(this);
    mNotesToggleWidget->show();
  }
  layoutChildren();
}

void VideoStage::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  layoutChildren();
}

void VideoStage::layoutChildren() {
  if (mRemoteWidget) {
    mRemoteWidget->setGeometry(rect());
  }
  if (mLocalPreviewWidget) {
    mLocalPreviewWidget->setGeometry(width() - kLocalPreviewWidth - kLocalPreviewMargin,
                                      height() - kLocalPreviewHeight - kLocalPreviewMargin,
                                      kLocalPreviewWidth, kLocalPreviewHeight);
    mLocalPreviewWidget->raise();
  }
  if (mControlBarWidget) {
    const QSize hint = mControlBarWidget->sizeHint();
    const int barWidth = hint.width() > 0 ? hint.width() : mControlBarWidget->width();
    const int barHeight = 48;
    const int x = (width() - barWidth) / 2;
    const int y = height() - 20 - barHeight;
    mControlBarWidget->setGeometry(x, y, barWidth, barHeight);
    mControlBarWidget->raise();
  }
  if (mNotesToggleWidget) {
    constexpr int kSize = 40;
    constexpr int kMargin = 12;
    mNotesToggleWidget->setGeometry(width() - kMargin - kSize, kMargin, kSize, kSize);
    mNotesToggleWidget->raise();
  }
  // Re-raise the self-preview above the new control-bar/notes-toggle slots
  // in case their geometries ever come to overlap; free given they don't
  // today, and it protects against future constant drift.
  if (mLocalPreviewWidget) {
    mLocalPreviewWidget->raise();
  }
}

} // namespace pcm::video::detail

CallPage::CallPage(pcm::video::DeviceManager *deviceManager, QWidget *parent) : QWidget(parent) {
  mDeviceManager = deviceManager;
  auto *outer = new QVBoxLayout(this);
  mStack = new QStackedWidget(this);
  outer->addWidget(mStack);

  buildDeviceCheckScreen(deviceManager);

  mConnectingScreen = new QWidget(this);
  auto *connectingLayout = new QVBoxLayout(mConnectingScreen);
  connectingLayout->addStretch();
  auto *connectingSpinner = new pcm::widgets::BusySpinner(mConnectingScreen);
  connectingSpinner->setObjectName("connectingSpinner");
  auto *spinnerRow = new QHBoxLayout();
  spinnerRow->addStretch();
  spinnerRow->addWidget(connectingSpinner);
  spinnerRow->addStretch();
  connectingLayout->addLayout(spinnerRow);
  mConnectingLabel = new QLabel(tr("Connecting..."), mConnectingScreen);
  mConnectingLabel->setObjectName("connectingLabel");
  mConnectingLabel->setAlignment(Qt::AlignCenter);
  mConnectingLabel->setFont(QFont(QStringLiteral("Inter Display"), 20));
  connectingLayout->addWidget(mConnectingLabel);
  connectingLayout->addStretch();
  mStack->addWidget(mConnectingScreen);

  buildConnectedScreen();

  mReconnectingBanner = new QWidget(this);
  mReconnectingBanner->setObjectName("reconnectingBanner");
  mReconnectingBanner->setVisible(false);
  auto *reconnectingLayout = new QHBoxLayout(mReconnectingBanner);
  reconnectingLayout->addStretch();
  auto *reconnectingSpinner = new pcm::widgets::BusySpinner(mReconnectingBanner);
  reconnectingSpinner->setObjectName("reconnectingSpinner");
  reconnectingLayout->addWidget(reconnectingSpinner);
  mReconnectingLabel = new QLabel(tr("Reconnecting..."), mReconnectingBanner);
  mReconnectingLabel->setFont(QFont(QStringLiteral("Inter"), 16));
  reconnectingLayout->addWidget(mReconnectingLabel);
  reconnectingLayout->addStretch();
  outer->addWidget(mReconnectingBanner);

  mEndedScreen = new QWidget(this);
  auto *endedLayout = new QVBoxLayout(mEndedScreen);
  endedLayout->addStretch();
  auto *endedHeadline = new QLabel(tr("Call ended."), mEndedScreen);
  endedHeadline->setAlignment(Qt::AlignCenter);
  endedHeadline->setFont(QFont(QStringLiteral("Inter Display"), 20));
  endedLayout->addWidget(endedHeadline);
  // Shown only when the session failed with a reason (joinFailed()/
  // reconnectFailed()); a normal user-initiated leave shows nothing here.
  mEndedReasonLabel = new QLabel(mEndedScreen);
  mEndedReasonLabel->setObjectName("endedReasonLabel");
  mEndedReasonLabel->setWordWrap(true);
  mEndedReasonLabel->setAlignment(Qt::AlignCenter);
  mEndedReasonLabel->setFont(QFont(QStringLiteral("Inter"), 16));
  mEndedReasonLabel->setVisible(false);
  endedLayout->addWidget(mEndedReasonLabel);
  endedLayout->addStretch();
  mStack->addWidget(mEndedScreen);

  mStack->setCurrentWidget(mDeviceCheck);
}

void CallPage::buildDeviceCheckScreen(pcm::video::DeviceManager *deviceManager) {
  mDeviceCheck = new DeviceCheckWidget(deviceManager, this);
  mDeviceCheck->setObjectName("deviceCheckWidget");
  // Fix round 1: DeviceCheckWidget::joinRequested is the real join gate, per
  // the design spec (PrejoinCheck is a real device-review step with its own
  // Join button, not a screen the flow blows through automatically). Task 12
  // originally wired this to a placeholder mSession->join(QString(),
  // QString()) call, which was always dead/wrong (fired before a session
  // even existed to attach, with empty url/token). Task 13's first pass
  // instead had CallsPage call session->join(url, token) immediately once a
  // token arrived, bypassing this button entirely — which contradicted both
  // the design spec and Task 12's own note that "Task 13 replaces this
  // [placeholder] with the real flow" at the point where this button is
  // clicked. This relay is the fix: CallPage has no token client and no
  // url/token of its own, so it cannot call join() itself — it just forwards
  // "the user confirmed" to whoever attached the session (CallsPage), which
  // does hold the pending url/token and performs the actual join() call.
  connect(mDeviceCheck, &DeviceCheckWidget::joinRequested, this, &CallPage::joinConfirmed);
  connect(mDeviceCheck, &DeviceCheckWidget::backRequested, this, &CallPage::deviceCheckCanceled);
  mStack->addWidget(mDeviceCheck);
}

void CallPage::buildConnectedScreen() {
  // The connected screen's structure (leave button, side-panel host) is
  // built eagerly here — setSidePanelToggleVisible() must work immediately
  // after construction, before any session ever attaches (see
  // CallPageTest.SidePanelToggleHiddenByDefaultUntilMadeVisible) — but its
  // "connectedView" object name is deliberately NOT assigned yet.
  // CallPageTest.StartsOnDeviceCheckScreen requires findChild("connectedView")
  // to return nullptr before any call has actually connected, so the name is
  // assigned lazily in onSessionStateChanged() the first time this screen is
  // actually shown.
  mConnectedView = new QWidget(this);
  auto *layout = new QVBoxLayout(mConnectedView);

  // Slot 0 of mVideoRow holds the remote video. It starts out as a blank
  // CallPage-owned placeholder; updateRemoteVideoWidget() swaps in the
  // attached provider's real remoteVideoWidget() (the one the provider
  // actually attaches the subscribed remote track to) when it has one.
  mVideoRow = new QHBoxLayout();
  mVideoStage = new pcm::video::detail::VideoStage(mConnectedView);
  mVideoStage->setObjectName("videoStage");
  mRemoteVideoPlaceholder = new pcm::video::RemoteVideoRenderer(mVideoStage);
  mRemoteVideoPlaceholder->setObjectName("remoteVideoPlaceholder");
  mVideoStage->setRemoteWidget(mRemoteVideoPlaceholder);
  mActiveRemoteVideoWidget = mRemoteVideoPlaceholder;
  mVideoRow->addWidget(mVideoStage, 1);
  mSidePanelHost = new QWidget(mConnectedView);
  mSidePanelHost->setVisible(false);
  new QVBoxLayout(mSidePanelHost);
  mVideoRow->addWidget(mSidePanelHost);
  layout->addLayout(mVideoRow);

  mMicrophoneToggleButton = new QToolButton(mConnectedView);
  mMicrophoneToggleButton->setObjectName("microphoneToggleButton");
  mMicrophoneToggleButton->setCheckable(true);
  mMicrophoneToggleButton->setChecked(true);
  mMicrophoneToggleButton->setIcon(pcm::widgets::microphoneIcon(true));
  mMicrophoneToggleButton->setToolTip(tr("Mute microphone"));
  mMicrophoneToggleButton->setAccessibleName(tr("Mute microphone"));
  connect(mMicrophoneToggleButton, &QToolButton::toggled, this, [this](bool checked) {
    mMicrophoneToggleButton->setIcon(pcm::widgets::microphoneIcon(checked));
    const QString label = checked ? tr("Mute microphone") : tr("Unmute microphone");
    mMicrophoneToggleButton->setToolTip(label);
    mMicrophoneToggleButton->setAccessibleName(label);
    if (mSession && mSession->provider()) {
      mSession->provider()->setMicrophoneEnabled(checked);
    }
  });

  mCameraToggleButton = new QToolButton(mConnectedView);
  mCameraToggleButton->setObjectName("cameraToggleButton");
  mCameraToggleButton->setCheckable(true);
  mCameraToggleButton->setChecked(true);
  mCameraToggleButton->setIcon(pcm::widgets::cameraIcon(true));
  mCameraToggleButton->setToolTip(tr("Turn off camera"));
  mCameraToggleButton->setAccessibleName(tr("Turn off camera"));
  connect(mCameraToggleButton, &QToolButton::toggled, this, [this](bool checked) {
    mCameraToggleButton->setIcon(pcm::widgets::cameraIcon(checked));
    const QString label = checked ? tr("Turn off camera") : tr("Turn on camera");
    mCameraToggleButton->setToolTip(label);
    mCameraToggleButton->setAccessibleName(label);
    if (mSession && mSession->provider()) {
      mSession->provider()->setCameraEnabled(checked);
    }
  });

  mFullscreenToggleButton = new QToolButton(mConnectedView);
  mFullscreenToggleButton->setObjectName("fullscreenToggleButton");
  mFullscreenToggleButton->setCheckable(true);
  mFullscreenToggleButton->setIcon(pcm::widgets::fullscreenIcon(false));
  mFullscreenToggleButton->setToolTip(tr("Enter fullscreen"));
  mFullscreenToggleButton->setAccessibleName(tr("Enter fullscreen"));
  connect(mFullscreenToggleButton, &QToolButton::toggled, this, [this](bool checked) {
    mFullscreenToggleButton->setIcon(pcm::widgets::fullscreenIcon(checked));
    const QString label = checked ? tr("Exit fullscreen") : tr("Enter fullscreen");
    mFullscreenToggleButton->setToolTip(label);
    mFullscreenToggleButton->setAccessibleName(label);
    if (!window()) {
      return;
    }
    if (checked) {
      window()->showFullScreen();
    } else {
      window()->showNormal();
    }
  });

  mDevicesButton = new QToolButton(mConnectedView);
  mDevicesButton->setObjectName("devicesButton");
  mDevicesButton->setIcon(pcm::widgets::devicesIcon());
  mDevicesButton->setToolTip(tr("Switch camera, microphone, or speaker"));
  mDevicesButton->setAccessibleName(tr("Switch camera, microphone, or speaker"));
  connect(mDevicesButton, &QToolButton::clicked, this, &CallPage::showDevicesPopover);

  auto *leaveButton = new QPushButton(tr("Leave"), mConnectedView);
  leaveButton->setObjectName("leaveButton");
  connect(leaveButton, &QPushButton::clicked, this, &CallPage::leaveRequested);

  mControlBar = new QWidget(mVideoStage);
  mControlBar->setObjectName("controlBar");
  mControlBar->setAutoFillBackground(true);
  QPalette controlBarPalette = mControlBar->palette();
  controlBarPalette.setColor(QPalette::Window, QColor(20, 20, 20));
  mControlBar->setPalette(controlBarPalette);
  mControlBar->setStyleSheet(QStringLiteral("#controlBar { border-radius: 24px; }"));
  auto *controlBarLayout = new QHBoxLayout(mControlBar);
  controlBarLayout->setContentsMargins(12, 6, 12, 6);
  controlBarLayout->setSpacing(8);
  controlBarLayout->addWidget(mMicrophoneToggleButton);
  controlBarLayout->addWidget(mCameraToggleButton);
  controlBarLayout->addWidget(mDevicesButton);
  controlBarLayout->addWidget(mFullscreenToggleButton);
  controlBarLayout->addWidget(leaveButton);
  mVideoStage->setControlBarWidget(mControlBar);

  // Non-fatal, transient local-device notice — see mMediaErrorBanner's doc
  // comment in call_page.h. Hidden by default; onMediaError() shows it and
  // sets its text, independent of mReconnectingBanner and the ended screen.
  // Its position in mConnectedView's layout carries no meaning for other
  // code (setSidePanelToggleVisible() places the notes toggle on mVideoStage
  // directly, not by indexing into this layout).
  mMediaErrorBanner = new QLabel(mConnectedView);
  mMediaErrorBanner->setObjectName("mediaErrorBanner");
  mMediaErrorBanner->setWordWrap(true);
  mMediaErrorBanner->setVisible(false);
  layout->addWidget(mMediaErrorBanner);

  mStack->addWidget(mConnectedView);
}

void CallPage::attachSession(pcm::video::VideoSession *session) {
  // A previously attached session that is still alive must stop driving
  // this page (CallsPage normally destroys it first, which disconnects it
  // anyway — this covers any caller that doesn't).
  if (mSession) {
    disconnect(mSession.data(), nullptr, this, nullptr);
  }
  mSession = session;
  mLastFailureReason.clear();
  refreshEndedReason();
  connect(session, &pcm::video::VideoSession::stateChanged, this, &CallPage::onSessionStateChanged);
  connect(session, &pcm::video::VideoSession::joinFailed, this, &CallPage::onSessionFailureReason);
  connect(session, &pcm::video::VideoSession::reconnectFailed, this,
          &CallPage::onSessionFailureReason);
  // connectionLost() is terminal too (the state machine has already moved to
  // Failed by the time a UI observer sees this) — it reuses the exact same
  // ended-screen mechanism as joinFailed()/reconnectFailed().
  connect(session, &pcm::video::VideoSession::connectionLost, this,
          &CallPage::onSessionFailureReason);
  // mediaError() never ends the call — its own, separate, non-fatal banner.
  connect(session, &pcm::video::VideoSession::mediaError, this, &CallPage::onMediaError);
  updateRemoteVideoWidget();
  updateLocalPreviewWidget();
  if (mMicrophoneToggleButton && mSession->provider()) {
    const QSignalBlocker blocker(mMicrophoneToggleButton);
    mMicrophoneToggleButton->setChecked(mSession->provider()->isMicrophoneEnabled());
    mMicrophoneToggleButton->setIcon(pcm::widgets::microphoneIcon(mMicrophoneToggleButton->isChecked()));
    const QString label =
        mMicrophoneToggleButton->isChecked() ? tr("Mute microphone") : tr("Unmute microphone");
    mMicrophoneToggleButton->setToolTip(label);
    mMicrophoneToggleButton->setAccessibleName(label);
  }
  if (mCameraToggleButton && mSession->provider()) {
    const QSignalBlocker blocker(mCameraToggleButton);
    mCameraToggleButton->setChecked(mSession->provider()->isCameraEnabled());
    mCameraToggleButton->setIcon(pcm::widgets::cameraIcon(mCameraToggleButton->isChecked()));
    const QString label =
        mCameraToggleButton->isChecked() ? tr("Turn off camera") : tr("Turn on camera");
    mCameraToggleButton->setToolTip(label);
    mCameraToggleButton->setAccessibleName(label);
  }
}

void CallPage::updateRemoteVideoWidget() {
  QWidget *provided = nullptr;
  if (mSession && mSession->provider()) {
    provided = mSession->provider()->remoteVideoWidget();
  }
  QWidget *target = provided ? provided : mRemoteVideoPlaceholder;
  if (target == mActiveRemoteVideoWidget) {
    return;
  }

  // mActiveRemoteVideoWidget may already be null here: a borrowed provider
  // widget is destroyed together with its provider (e.g. CallsPage::
  // startJoin() replacing its session before attaching the new one).
  if (QWidget *previous = mActiveRemoteVideoWidget.data()) {
    previous->hide();
    if (previous != mRemoteVideoPlaceholder) {
      // Borrowed from a provider: hand it back instead of deleting it —
      // the provider still owns its lifetime.
      previous->setParent(nullptr);
    }
  }

  if (target->parentWidget() != mVideoStage) {
    target->setParent(mVideoStage);
  }
  mVideoStage->setRemoteWidget(target);
  // setParent() hides a widget, and a swapped-out placeholder was hidden
  // explicitly above; either way it has to be shown again.
  target->show();
  mActiveRemoteVideoWidget = target;
}

void CallPage::updateLocalPreviewWidget() {
  QWidget *provided = nullptr;
  if (mSession && mSession->provider()) {
    provided = mSession->provider()->localVideoWidget();
  }
  if (provided == mActiveLocalPreviewWidget) {
    return;
  }

  if (QWidget *previous = mActiveLocalPreviewWidget.data()) {
    previous->hide();
    previous->setParent(nullptr);
  }

  mActiveLocalPreviewWidget = provided;
  if (provided) {
    if (provided->parentWidget() != mVideoStage) {
      provided->setParent(mVideoStage);
    }
    provided->show();
  }
  mVideoStage->setLocalPreviewWidget(provided);
}

void CallPage::onSessionFailureReason(const QString &reason) {
  mLastFailureReason = reason;
  // VideoSession emits joinFailed()/reconnectFailed() before entering
  // Failed today, but don't depend on that ordering: if the reason arrives
  // after the ended screen is already up, show it there immediately.
  if (mStack->currentWidget() == mEndedScreen) {
    refreshEndedReason();
  }
}

void CallPage::onMediaError(const QString &reason) {
  // Testable synchronously: the banner's text/visibility are set here,
  // immediately, before any auto-hide timer fires — a test can assert on
  // them right after emitting mediaError() with no need to wait.
  mMediaErrorBanner->setText(reason);
  mMediaErrorBanner->setVisible(true);
  QTimer::singleShot(kMediaErrorBannerAutoHideMs, this, [this, reason]() {
    // Only auto-hide if this is still the most recent reason shown — a
    // fresh mediaError() (or another call to this slot) in the meantime
    // already reset the timer's effect by overwriting the text, so an
    // earlier timer firing later must not blank a newer message.
    if (mMediaErrorBanner->text() == reason) {
      mMediaErrorBanner->setVisible(false);
    }
  });
}

void CallPage::showDevicesPopover() {
  auto *menu = new QMenu(this);
  menu->setAttribute(Qt::WA_DeleteOnClose);

  // Builds one combo row (object name + entries + selection callback) and
  // adds it to `menu` via a QWidgetAction, since QMenu cannot host a plain
  // QComboBox as a regular action. Devices are re-looked-up by id inside
  // each callback (via QMediaDevices::videoInputs()/audioInputs()/
  // audioOutputs()) rather than captured by value, because
  // DeviceManager::cameras()/microphones()/speakers() are themselves
  // thin, live-queried wrappers over QMediaDevices (see device_manager.h) —
  // re-querying is as cheap as using a captured snapshot, and stays correct
  // if a device is unplugged between opening the popover and picking an
  // entry.
  auto addDeviceRow = [menu](const QString &objectName, const auto &devices, const QByteArray &currentId,
                              auto onSelected) -> QComboBox * {
    auto *combo = new QComboBox(menu);
    combo->setObjectName(objectName);
    for (const auto &device : devices) {
      combo->addItem(device.description(), device.id());
      if (device.id() == currentId) {
        combo->setCurrentIndex(combo->count() - 1);
      }
    }
    QObject::connect(combo, &QComboBox::currentIndexChanged, menu, [combo, onSelected](int index) {
      if (index < 0) {
        return;
      }
      onSelected(combo->itemData(index).toByteArray());
    });
    auto *action = new QWidgetAction(menu);
    action->setDefaultWidget(combo);
    menu->addAction(action);
    return combo;
  };

  auto *provider = mSession ? mSession->provider() : nullptr;

  mDeviceCameraCombo = addDeviceRow(QStringLiteral("deviceCameraCombo"), mDeviceManager->cameras(),
                                     QByteArray(), [provider](const QByteArray &id) {
                                       if (!provider) {
                                         return;
                                       }
                                       for (const auto &device : QMediaDevices::videoInputs()) {
                                         if (device.id() == id) {
                                           provider->switchCamera(device);
                                           return;
                                         }
                                       }
                                     });
  mDeviceMicrophoneCombo = addDeviceRow(QStringLiteral("deviceMicrophoneCombo"), mDeviceManager->microphones(),
                                         QByteArray(), [provider](const QByteArray &id) {
                                           if (!provider) {
                                             return;
                                           }
                                           for (const auto &device : QMediaDevices::audioInputs()) {
                                             if (device.id() == id) {
                                               provider->switchMicrophone(device);
                                               return;
                                             }
                                           }
                                         });
  mDeviceSpeakerCombo = addDeviceRow(QStringLiteral("deviceSpeakerCombo"), mDeviceManager->speakers(),
                                      QByteArray(), [provider](const QByteArray &id) {
                                        if (!provider) {
                                          return;
                                        }
                                        for (const auto &device : QMediaDevices::audioOutputs()) {
                                          if (device.id() == id) {
                                            provider->switchSpeaker(device);
                                            return;
                                          }
                                        }
                                      });

  menu->popup(mDevicesButton->mapToGlobal(QPoint(0, mDevicesButton->height())));
}

void CallPage::refreshEndedReason() {
  mEndedReasonLabel->setText(mLastFailureReason);
  mEndedReasonLabel->setVisible(!mLastFailureReason.isEmpty());
}

void CallPage::setSidePanelWidget(QWidget *panel) {
  if (mSidePanel) {
    mSidePanelHost->layout()->removeWidget(mSidePanel);
    mSidePanel->setParent(nullptr);
  }
  mSidePanel = panel;
  if (panel) {
    mSidePanelHost->layout()->addWidget(panel);
  }
}

void CallPage::setSidePanelToggleVisible(bool visible) {
  if (visible && !mNotesToggleButton) {
    mNotesToggleButton = new QToolButton(mVideoStage);
    mNotesToggleButton->setObjectName("notesToggleButton");
    mNotesToggleButton->setCheckable(true);
    mNotesToggleButton->setIcon(pcm::widgets::notesIcon());
    mNotesToggleButton->setToolTip(tr("Notes"));
    mNotesToggleButton->setAccessibleName(tr("Notes"));
    mNotesToggleButton->setAutoFillBackground(true);
    QPalette notesPalette = mNotesToggleButton->palette();
    notesPalette.setColor(QPalette::Button, QColor(20, 20, 20));
    mNotesToggleButton->setPalette(notesPalette);
    mNotesToggleButton->setStyleSheet(QStringLiteral("#notesToggleButton { border-radius: 20px; }"));
    connect(mNotesToggleButton, &QToolButton::toggled, this,
            [this](bool checked) { mSidePanelHost->setVisible(checked); });
    mVideoStage->setNotesToggleWidget(mNotesToggleButton);
  } else if (!visible && mNotesToggleButton) {
    delete mNotesToggleButton;
    mNotesToggleButton = nullptr;
  }
}

void CallPage::setSidePanelExpandedByDefault(bool expanded) {
  if (mNotesToggleButton) {
    mNotesToggleButton->setChecked(expanded);
  }
}

void CallPage::onSessionStateChanged(const pcm::video::VideoSessionState state) {
  using pcm::video::VideoSessionState;
  mReconnectingBanner->setVisible(state == VideoSessionState::Reconnecting);

  switch (state) {
  case VideoSessionState::NoMeeting:
  case VideoSessionState::Provisioned:
  case VideoSessionState::PrejoinCheck:
    mStack->setCurrentWidget(mDeviceCheck);
    break;
  case VideoSessionState::Joining:
    mConnectingLabel->setText(tr("Connecting..."));
    mStack->setCurrentWidget(mConnectingScreen);
    break;
  case VideoSessionState::WaitingForClient:
    mConnectingLabel->setText(tr("Waiting for the other participant to join..."));
    mStack->setCurrentWidget(mConnectingScreen);
    break;
  case VideoSessionState::Connected:
  case VideoSessionState::Reconnecting:
    mConnectedView->setObjectName("connectedView");
    mStack->setCurrentWidget(mConnectedView);
    break;
  case VideoSessionState::Leaving:
    break;
  case VideoSessionState::Ended:
  case VideoSessionState::Failed:
    refreshEndedReason();
    mStack->setCurrentWidget(mEndedScreen);
    emit callEnded();
    break;
  }
}
