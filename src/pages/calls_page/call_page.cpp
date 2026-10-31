#include "call_page.h"
#include "accent_color.h"

#include "call_control_icons.h"
#include "call_layout_strategy.h"
#include <algorithm>

#include <QAudioDevice>
#include <QCameraDevice>
#include <QAction>
#include <QComboBox>
#include "combo_fit.h"
#include <QGuiApplication>
#include <QScreen>
#include <QWindowCapture>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QMenu>
#include <QOpenGLWidget>
#include <QPoint>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QButtonGroup>

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

VideoStage::VideoStage(QWidget *parent) : QWidget(parent), mTileHost(new QWidget(this)),
    mTileGrid(new QGridLayout(mTileHost)) {
  mTileHost->setObjectName("participantTileHost");
  mTileGrid->setContentsMargins(0, 0, 0, 0);
  mTileGrid->setSpacing(8);
}

void VideoStage::setTiles(const QVector<ParticipantTile *> &tiles) {
  mTiles = tiles;
  layoutChildren();
}

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

void VideoStage::setWaitingBanner(QWidget *widget) {
  if (mWaitingBanner == widget) {
    return;
  }
  if (mWaitingBanner) {
    mWaitingBanner->setParent(nullptr);
  }
  mWaitingBanner = widget;
  if (mWaitingBanner) {
    mWaitingBanner->setParent(this);
  }
  layoutChildren();
}

void VideoStage::setSharingBanner(QWidget *widget) {
  if (mSharingBanner == widget) return;
  if (mSharingBanner) mSharingBanner->setParent(nullptr);
  mSharingBanner = widget;
  if (mSharingBanner) mSharingBanner->setParent(this);
  layoutChildren();
}

void VideoStage::setFeaturedScreen(const QString &key) {
  if (mFeaturedScreen == key) return;
  mFeaturedScreen = key;
  layoutChildren();
}

void VideoStage::setContentWidget(QWidget *widget) {
  if (mContentWidget == widget)
    return;
  if (mContentWidget)
    mContentWidget->setParent(nullptr);
  mContentWidget = widget;
  if (mContentWidget) {
    mContentWidget->setParent(this);
    mContentWidget->lower();
    mContentWidget->show();
  }
  mTileHost->setVisible(!mContentWidget);
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

namespace {
// The control bar floats this far above the bottom edge, and the tiles keep clear of the bar
// plus a small gap -- tight enough that no dead band is left below the video.
constexpr int kControlBarHeight = 48;
constexpr int kControlBarBottomMargin = 12;
constexpr int kBottomReserved = kControlBarBottomMargin + kControlBarHeight + 8;
} // namespace

void VideoStage::layoutChildren() {
  const bool sharingStatus = mLayoutControls && mSharingBanner && !mSharingBanner->isHidden();
  const int top = sharingStatus ? 104 : (mLayoutControls || (mNotesToggleWidget && !mNotesToggleWidget->isHidden())) ? 64 : 12;
  if (mContentWidget) {
    // The web page has its own toolbar along the bottom and the notes toggle sits at the top:
    // keep the page clear of both bands so the native overlays never block its clicks.
    mContentWidget->setGeometry(0, top, width(), std::max(0, height() - top - kBottomReserved));
    mContentWidget->lower();
  }
  while (auto *item = mTileGrid->takeAt(0))
    delete item;
  for (int column = 0; column < mTileGrid->columnCount(); ++column) {
    mTileGrid->setColumnMinimumWidth(column, 0);
    mTileGrid->setColumnStretch(column, 0);
  }
  for (int row = 0; row < mTileGrid->rowCount(); ++row) {
    mTileGrid->setRowMinimumHeight(row, 0);
    mTileGrid->setRowStretch(row, 0);
  }
  QVector<ParticipantTile *> tiles;
  for (auto *tile : mTiles) {
    if (mContentWidget) { tile->hide(); continue; }
    if (mViewLayout != CallViewLayout::Auto && tile->isScreen()) tile->hide();
    else tiles.append(tile);
  }
  ParticipantTile *local = nullptr;
  for (auto *tile : tiles)
    if (tile->isLocal() && !tile->isScreen())
      local = tile;
  const bool solo = tiles.size() == 1 || (tiles.size() == 2 && local && mViewLayout != CallViewLayout::Grid && mViewLayout != CallViewLayout::Side);
  // Waiting alone (just your own tile) is inset like the group grid, so its rounded corners show;
  // in a 1:1 call the main tile still fills the stage.
  const bool fullBleed = solo && tiles.size() == 2;
  for (auto *tile : tiles)
    tile->setNameTopMargin(fullBleed && mLayoutControls && !tile->isLocal() ? top : 8);
  const QRect available = fullBleed ? rect() :
      QRect(12, top, std::max(0, width() - 24), std::max(0, height() - top - kBottomReserved));
  ParticipantTile *featured = nullptr;
  for (auto *tile : tiles)
    if (tile->isScreen() && (!featured || tile->key() == mFeaturedScreen))
      featured = tile;
  if (mViewLayout == CallViewLayout::Side) {
    QVector<ParticipantTile *> remotes;
    for (auto *tile : tiles) {
      if (tile->isScreen()) tile->hide();
      else if (!tile->isLocal()) remotes.append(tile);
    }
    mTileHost->setGeometry(available);
    const int sideWidth = local ? std::min(available.width() / 3, std::clamp(available.width() / 5, 120, 240)) : 0;
    const int mainWidth = std::max(0, available.width() - sideWidth - (local ? 8 : 0));
    const auto strategy = CallLayoutStrategy::select(remotes.size(), false, QSize(mainWidth, available.height()));
    const int gridHeight = strategy.rows * strategy.tileSize.height() + std::max(0, strategy.rows - 1) * 8;
    for (int i = 0; i < remotes.size() && strategy.columns > 0; ++i) {
      auto *tile = remotes[i];
      const int row = i / strategy.columns;
      const int count = std::min(strategy.columns, int(remotes.size()) - row * strategy.columns);
      const int rowWidth = count * strategy.tileSize.width() + (count - 1) * 8;
      tile->setParent(mTileHost);
      const QSize size = remotes.size() == 1 ? QSize(mainWidth, available.height()) : strategy.tileSize;
      tile->setFixedSize(size);
      tile->setCornerRadius(12);
      tile->move(remotes.size() == 1 ? 0 : (mainWidth - rowWidth) / 2 + (i % strategy.columns) * (size.width() + 8),
                 remotes.size() == 1 ? 0 : (available.height() - gridHeight) / 2 + row * (size.height() + 8));
      tile->show();
    }
    if (local) {
      local->setParent(mTileHost);
      local->setFixedSize(sideWidth, std::min(available.height(), sideWidth * 9 / 16));
      local->setCornerRadius(12);
      local->move(mainWidth + 8, 0);
      local->show();
    }
  } else if (featured && !fullBleed) {
    constexpr int kGap = 8;
    constexpr int kTileCornerRadius = 12;
    QVector<ParticipantTile *> strip;
    for (auto *tile : tiles)
      if (tile != featured) strip.append(tile);
    mTileHost->setGeometry(available);
    int stripHeight = 0;
    if (!strip.isEmpty())
      stripHeight = std::clamp(available.height() / 5, 80, 150);
    const int stageHeight = std::max(0, available.height() - (strip.isEmpty() ? 0 : stripHeight + kGap));
    featured->setParent(mTileHost);
    featured->setFixedSize(available.width(), stageHeight);
    featured->setCornerRadius(kTileCornerRadius);
    featured->move(0, 0);
    featured->show();
    if (!strip.isEmpty()) {
      const int count = strip.size();
      const int width = std::max(0, std::min(stripHeight * 16 / 9, (available.width() - (count - 1) * kGap) / count));
      const int total = count * width + (count - 1) * kGap;
      int x = std::max(0, (available.width() - total) / 2);
      for (auto *tile : strip) {
        tile->setParent(mTileHost);
        tile->setFixedSize(width, stripHeight);
        tile->setCornerRadius(kTileCornerRadius);
        tile->move(x, stageHeight + kGap);
        tile->show();
        x += width + kGap;
      }
    }
  } else {
  const bool overlay = mViewLayout == CallViewLayout::Overlay && local && tiles.size() > 1;
  auto strategy = CallLayoutStrategy::select(tiles.size() - (overlay ? 1 : 0),
      mViewLayout == CallViewLayout::Auto && local != nullptr, available.size());
  if (overlay) strategy.pictureInPicture = true;
  if (solo) strategy.tileSize = available.size();
  constexpr int kTileCornerRadius = 12;
  const int gridCount = tiles.size() - (strategy.pictureInPicture ? 1 : 0);
  const QSize gridSize(strategy.columns * strategy.tileSize.width() + std::max(0, strategy.columns - 1) * 8,
                       strategy.rows * strategy.tileSize.height() + std::max(0, strategy.rows - 1) * 8);
  mTileHost->setGeometry(solo ? available : QRect(available.center() - QPoint(gridSize.width() / 2, gridSize.height() / 2), gridSize));
  int index = 0;
  for (auto *tile : tiles) {
    if (strategy.pictureInPicture && tile == local) {
      tile->setParent(this);
      const int pipWidth = std::min(std::max(0, width() / 3), std::clamp(width() / 5, 200, 280));
      const int pipHeight = pipWidth * 9 / 16;
      tile->setFixedSize(pipWidth, pipHeight);
      tile->setCornerRadius(kTileCornerRadius);
      tile->setGeometry(std::max(0, width() - pipWidth - 20), std::max(0, height() - pipHeight - kBottomReserved),
                        pipWidth, pipHeight);
      tile->show();
      tile->raise();
      continue;
    }
    tile->setParent(mTileHost);
    tile->setFixedSize(strategy.tileSize);
    tile->setCornerRadius(fullBleed ? 0 : kTileCornerRadius);
    if (strategy.columns > 0) {
      const int row = index / strategy.columns;
      const int rowCount = std::min(strategy.columns, gridCount - row * strategy.columns);
      const int offset = strategy.columns - rowCount;
      mTileGrid->addWidget(tile, row, offset + 2 * (index % strategy.columns), 1, 2);
    }
    tile->show();
    ++index;
  }
  for (int column = 0; column < strategy.columns * 2; ++column)
    mTileGrid->setColumnMinimumWidth(column, std::max(0, (strategy.tileSize.width() - 8) / 2));
  mTileGrid->activate();
  }
  if (mLayoutControls) {
    const QSize hint = mLayoutControls->sizeHint();
    mLayoutControls->setGeometry(12, 12, std::min(std::max(0, width() - 24), hint.width()), hint.height());
    mLayoutControls->raise();
  }
  if (mSharingBanner && !mSharingBanner->isHidden()) {
    const QSize hint = mSharingBanner->sizeHint();
    mSharingBanner->setGeometry((width() - hint.width()) / 2, mLayoutControls ? 56 : 12, hint.width(), hint.height());
    mSharingBanner->raise();
  }
  if (mControlBarWidget) {
    const QSize hint = mControlBarWidget->sizeHint();
    const int barWidth = std::min(width(), hint.width() > 0 ? hint.width() : mControlBarWidget->width());
    const int barHeight = kControlBarHeight;
    const int x = (width() - barWidth) / 2;
    const int y = height() - kControlBarBottomMargin - barHeight;
    mControlBarWidget->setGeometry(x, y, barWidth, barHeight);
    mControlBarWidget->raise();
  }
  if (mWaitingBanner) {
    const QSize hint = mWaitingBanner->sizeHint();
    const int barTop = height() - kControlBarBottomMargin - kControlBarHeight;
    mWaitingBanner->setGeometry((width() - hint.width()) / 2, barTop - 12 - hint.height(),
                                hint.width(), hint.height());
    mWaitingBanner->raise();
  }
  if (mNotesToggleWidget) {
    constexpr int kSize = 40;
    constexpr int kMargin = 12;
    mNotesToggleWidget->setGeometry(width() - kMargin - kSize, kMargin, kSize, kSize);
    mNotesToggleWidget->raise();
  }
}

} // namespace pcm::video::detail

namespace {
constexpr int kSidePanelMinWidth = 200;
constexpr int kSidePanelDefaultWidth = 440;
} // namespace

CallPage::CallPage(pcm::video::DeviceManager *deviceManager, QWidget *parent) : QWidget(parent) {
  mDeviceManager = deviceManager;
  // Since Qt 6.4 the first QOpenGLWidget added to a shown window recreates its
  // native window (raster -> OpenGL surface), so joining a call from the
  // preview made the whole window vanish and reappear. A hidden one created
  // before the window is shown gives it the OpenGL surface from the start.
  auto *glSurface = new QOpenGLWidget(this);
  glSurface->setObjectName("glSurfaceAnchor");
  glSurface->hide();
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
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
  auto *endedSplit = new QHBoxLayout(mEndedScreen);
  auto *endedContent = new QWidget(mEndedScreen);
  endedSplit->addWidget(endedContent, 1);
  auto *endedLayout = new QVBoxLayout(endedContent);
  mEndedSidePanelHost = new QWidget(mEndedScreen);
  mEndedSidePanelHost->setObjectName("endedSidePanelHost");
  mEndedSidePanelHost->setMinimumWidth(kSidePanelMinWidth);
  mEndedSidePanelHost->setMaximumWidth(520);
  auto *endedPanelLayout = new QVBoxLayout(mEndedSidePanelHost);
  endedPanelLayout->setContentsMargins(0, 0, 0, 0);
  endedSplit->addWidget(mEndedSidePanelHost);
  mEndedSidePanelHost->hide();
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
  auto *backToCalls = new QPushButton(tr("Back to calls"), endedContent);
  backToCalls->setObjectName("endedBackToCalls");
  connect(backToCalls, &QPushButton::clicked, this, &CallPage::backToCallsRequested);
  endedLayout->addWidget(backToCalls, 0, Qt::AlignCenter);
  endedLayout->addStretch();
  mStack->addWidget(mEndedScreen);

  mStack->setCurrentWidget(mDeviceCheck);
}

void CallPage::setSidePanelOpen(bool open) {
  mSidePanelHost->setVisible(open);
  if (open && !mSidePanelSized) {
    // Default to a comfortable notes width the first time; after that, the splitter keeps the
    // width the user chose, even across hiding and showing the panel.
    mSidePanelSized = true;
    const int total = std::max(mVideoSplitter->width(), kSidePanelDefaultWidth + kSidePanelMinWidth);
    mVideoSplitter->setSizes({total - kSidePanelDefaultWidth, kSidePanelDefaultWidth});
  }
  arrangeNotesLayout();
}

void CallPage::setNotesFocused(bool focused) {
  mNotesFocused = focused;
  if (mNotesFocusButton) {
    const QSignalBlocker blocker(mNotesFocusButton);
    mNotesFocusButton->setChecked(focused);
    mNotesFocusButton->setText(focused ? tr("Video and notes") : tr("Expand notes"));
  }
  arrangeNotesLayout();
}

void CallPage::arrangeNotesLayout() {
  if (!mVideoSplitter || !mVideoStage) return;
  const bool compact = width() < 760;
  const bool smallVideo = !mSidePanelHost->isHidden() && (mNotesFocused || compact);
  mVideoSplitter->setOrientation(smallVideo ? Qt::Vertical : Qt::Horizontal);
  mVideoStage->setMaximumHeight(smallVideo ? 240 : QWIDGETSIZE_MAX);
  mSidePanelHost->setMinimumWidth(compact ? 0 : kSidePanelMinWidth);
  if (smallVideo) mVideoSplitter->setSizes({240, std::max(300, height() - 240)});
  mVideoStage->refreshLayout();
}

void CallPage::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  arrangeNotesLayout();
}

void CallPage::setFullscreen(bool fullscreen) {
  // Only the participants and the control bar stay: the notes panel and its toggle go too.
  if (mNotesToggleButton) {
    mNotesToggleButton->setVisible(!fullscreen);
  }
  setSidePanelOpen(!fullscreen && mNotesToggleButton && mNotesToggleButton->isChecked());
  mVideoStage->update();
  if (auto *topLevel = window()) {
    if (fullscreen) {
      topLevel->showFullScreen();
    } else {
      topLevel->showNormal();
    }
  }
  emit fullscreenChanged(fullscreen);
}

void CallPage::leaveFullscreenIfActive() {
  if (mFullscreenToggleButton && mFullscreenToggleButton->isChecked()) {
    mFullscreenToggleButton->setChecked(false);
  }
}

void CallPage::applyDeviceCheckSelection() {
  auto *provider = mSession ? mSession->provider() : nullptr;
  if (!provider || !mDeviceCheck) {
    return;
  }
  if (const auto camera = mDeviceCheck->selectedCamera()) provider->switchCamera(*camera);
  if (const auto microphone = mDeviceCheck->selectedMicrophone()) provider->switchMicrophone(*microphone);
  if (const auto speaker = mDeviceCheck->selectedSpeaker()) provider->switchSpeaker(*speaker);
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
  // The devices picked on this screen must be the ones the call opens, so hand them to the
  // provider before CallsPage performs the join.
  connect(mDeviceCheck, &DeviceCheckWidget::joinRequested, this, [this]() {
    applyDeviceCheckSelection();
    emit joinConfirmed();
  });
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
  layout->setContentsMargins(0, 0, 0, 0);

  mVideoSplitter = new QSplitter(Qt::Horizontal, mConnectedView);
  mVideoSplitter->setObjectName("videoSplitter");
  mVideoSplitter->setChildrenCollapsible(false);
  mVideoSplitter->setHandleWidth(8);
  mVideoStage = new pcm::video::detail::VideoStage(mVideoSplitter);
  mVideoStage->setObjectName("videoStage");
  mSidePanelHost = new QWidget(mVideoSplitter);
  mSidePanelHost->setObjectName("sidePanelHost");
  // The notes panel can be dragged narrower, but never so narrow it becomes unusable.
  mSidePanelHost->setMinimumWidth(kSidePanelMinWidth);
  mSidePanelHost->setVisible(false);
  auto *notesLayout = new QVBoxLayout(mSidePanelHost);
  notesLayout->setContentsMargins(0, 0, 0, 0);
  mNotesTools = new QWidget(mSidePanelHost);
  auto *notesTools = new QHBoxLayout(mNotesTools); notesTools->setContentsMargins(8, 4, 8, 4);
  mNotesFocusButton = new QToolButton(mNotesTools);
  mNotesFocusButton->setObjectName("expandCallNotes"); mNotesFocusButton->setText(tr("Expand notes"));
  mNotesFocusButton->setCheckable(true);
  connect(mNotesFocusButton, &QToolButton::toggled, this, &CallPage::setNotesFocused);
  notesTools->addWidget(mNotesFocusButton); notesTools->addStretch();
  notesLayout->addWidget(mNotesTools);
  mVideoSplitter->addWidget(mVideoStage);
  mVideoSplitter->addWidget(mSidePanelHost);
  mVideoSplitter->setStretchFactor(0, 1);
  mVideoSplitter->setStretchFactor(1, 0);
  layout->addWidget(mVideoSplitter, 1);
  // "Waiting for others to join": an opaque status pill floating over the video stage, above the
  // control bar (opaque because it overlays a QOpenGLWidget renderer), instead of a text line
  // that took its own strip below the video.
  mWaitingBanner = new QWidget(mVideoStage);
  mWaitingBanner->setObjectName("waitingBanner");
  mWaitingBanner->setStyleSheet(
      QStringLiteral("#waitingBanner { background-color: rgb(20, 20, 20); border-radius: 18px; }"));
  auto *waitingLayout = new QHBoxLayout(mWaitingBanner);
  waitingLayout->setContentsMargins(16, 8, 18, 8);
  waitingLayout->setSpacing(8);
  auto *waitingDot = new QLabel(QStringLiteral("\u25CF"), mWaitingBanner);
  waitingDot->setStyleSheet(QStringLiteral("color: #ffb020;"));
  waitingLayout->addWidget(waitingDot);
  mWaitingLabel = new QLabel(tr("Waiting for others to join"), mWaitingBanner);
  mWaitingLabel->setObjectName("waitingLabel");
  mWaitingLabel->setTextFormat(Qt::PlainText);
  waitingLayout->addWidget(mWaitingLabel);
  mWaitingBanner->hide();
  mVideoStage->setWaitingBanner(mWaitingBanner);

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
      refreshMediaButtons();
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
      refreshMediaButtons();
    }
  });

  mFullscreenToggleButton = new QToolButton(mConnectedView);
  auto *layoutControls = new QWidget(mVideoStage);
  layoutControls->setObjectName("layoutControls");
  auto *viewButtons = new QHBoxLayout(layoutControls);
  viewButtons->setContentsMargins(0, 0, 0, 0);
  viewButtons->setSpacing(4);
  auto *viewGroup = new QButtonGroup(layoutControls);
  using pcm::video::detail::CallViewLayout;
  const QStringList labels{tr("Auto"), tr("Grid"), tr("Overlay"), tr("Side")};
  const QStringList names{"layoutAutoButton", "layoutGridButton", "layoutOverlayButton", "layoutSideButton"};
  layoutControls->setStyleSheet(QStringLiteral(
      "QToolButton {background: rgb(50,54,65); color: white; border: none; border-radius: 8px; padding: 8px;}"
      "QToolButton:checked {background: %1;}").arg(pcm::widgets::cssRgba(pcm::widgets::accentColor().darker(120))));
  for (int i = 0; i < labels.size(); ++i) {
    auto *button = new QToolButton(layoutControls);
    button->setObjectName(names[i]);
    button->setText(labels[i]);
    button->setAccessibleName(labels[i]);
    button->setToolTip(labels[i]);
    button->setCheckable(true);
    viewGroup->addButton(button, i);
    viewButtons->addWidget(button);
    if (i == 0) button->setChecked(true);
    connect(button, &QToolButton::toggled, this, [this, i](bool checked) {
      if (checked) mVideoStage->setViewLayout(static_cast<CallViewLayout>(i));
    });
  }
  layoutControls->show();
  mVideoStage->setLayoutControls(layoutControls);
  mLayoutControls = layoutControls;
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
    setFullscreen(checked);
  });
  // Esc leaves fullscreen, the usual way out when the control bar is hidden behind the video.
  auto *escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), mConnectedView);
  escapeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(escapeShortcut, &QShortcut::activated, this, [this]() { leaveFullscreenIfActive(); });

  mScreenShareButton = new QToolButton(mConnectedView);
  mScreenShareButton->setObjectName("screenShareButton");
  mScreenShareButton->setCheckable(true);
  mScreenShareButton->setIcon(pcm::widgets::screenShareIcon(false));
  mScreenShareButton->setToolTip(tr("Share screen"));
  mScreenShareButton->setAccessibleName(tr("Share screen"));
  // The checked state follows the provider (a failed or ended capture unchecks it), not the click.
  connect(mScreenShareButton, &QToolButton::clicked, this, [this] {
    auto *provider = mSession ? mSession->provider() : nullptr;
    if (!provider) {
      refreshMediaButtons();
      return;
    }
    if (provider->isScreenSharing()) {
      provider->stopScreenShare();
      return;
    }
    refreshMediaButtons();
    showScreenShareMenu();
  });

  mSharingBanner = new QWidget(mConnectedView);
  mSharingBanner->setObjectName("sharingBanner");
  mSharingBanner->setStyleSheet("#sharingBanner { background: #534AB7; border-radius: 8px; } QLabel { color: white; } "
                                "QPushButton { color: white; border: none; text-decoration: underline; background: transparent; }");
  auto *sharingLayout = new QHBoxLayout(mSharingBanner);
  sharingLayout->setContentsMargins(12, 5, 12, 5);
  auto *sharingLabel = new QLabel(tr("You are sharing your screen"), mSharingBanner);
  sharingLabel->setTextFormat(Qt::PlainText);
  auto *stopSharing = new QPushButton(tr("Stop"), mSharingBanner);
  stopSharing->setObjectName("sharingBannerStop");
  stopSharing->setCursor(Qt::PointingHandCursor);
  connect(stopSharing, &QPushButton::clicked, this, [this] {
    if (mSession && mSession->provider()) mSession->provider()->stopScreenShare();
  });
  sharingLayout->addWidget(sharingLabel);
  sharingLayout->addWidget(stopSharing);
  mSharingBanner->hide();
  mVideoStage->setSharingBanner(mSharingBanner);

  mDevicesButton = new QToolButton(mConnectedView);
  mDevicesButton->setObjectName("devicesButton");
  mDevicesButton->setIcon(pcm::widgets::devicesIcon());
  mDevicesButton->setToolTip(tr("Switch camera, microphone, or speaker"));
  mDevicesButton->setAccessibleName(tr("Switch camera, microphone, or speaker"));
  connect(mDevicesButton, &QToolButton::clicked, this, &CallPage::showDevicesPopover);

  auto *leaveButton = new QPushButton(tr("Leave"), mConnectedView);
  leaveButton->setObjectName("leaveButton");
  leaveButton->setFixedHeight(40);
  leaveButton->setStyleSheet("QPushButton { background: #b52b3a; color: white; border: none; border-radius: 20px; padding: 0 20px; } QPushButton:hover { background: #d33547; }");
  connect(leaveButton, &QPushButton::clicked, this, &CallPage::leaveRequested);

  mControlBar = new QWidget(mVideoStage);
  mControlBar->setObjectName("controlBar");
  // The fill has to come from the stylesheet: once a QSS rule matches,
  // QStyleSheetStyle owns background painting and an autoFillBackground/
  // QPalette fill is never painted. Fully opaque (no alpha) because the bar
  // overlays a QOpenGLWidget video renderer.
  // Round 40x40 buttons: dark when off, accent when on (checked), a touch lighter on hover.
  // Deeper accent than the plain Highlight so the light glyphs stay readable on it.
  const QColor checkedOn = pcm::widgets::accentColor().darker(120);
  mControlBar->setStyleSheet(QStringLiteral(
      "#controlBar { background-color: rgb(20, 20, 20); border-radius: 24px; }"
      "#controlBar QToolButton { background-color: rgb(50, 54, 65); border: none; border-radius: 20px; }"
      "#controlBar QToolButton:hover { background-color: rgb(68, 73, 87); }"
      "#controlBar QToolButton:checked { background-color: %1; }"
      "#controlBar QToolButton:checked:hover { background-color: %2; }")
          .arg(pcm::widgets::cssRgba(checkedOn), pcm::widgets::cssRgba(checkedOn.lighter(115))));
  auto *controlBarLayout = new QHBoxLayout(mControlBar);
  controlBarLayout->setContentsMargins(12, 4, 12, 4);
  controlBarLayout->setSpacing(8);
  for (auto *button : {mMicrophoneToggleButton, mCameraToggleButton, mScreenShareButton, mDevicesButton, mFullscreenToggleButton}) {
    button->setFixedSize(40, 40);
    button->setIconSize(QSize(20, 20));
  }
  controlBarLayout->addWidget(mMicrophoneToggleButton);
  controlBarLayout->addWidget(mCameraToggleButton);
  controlBarLayout->addWidget(mScreenShareButton);
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
  clearParticipants();
  setWebView(nullptr);
  mSession = session;
  if (!session)
    return;
  setWebView(session->provider()->callView());
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
  connect(session, &QObject::destroyed, this, [this] {
    mSession = nullptr;
    clearParticipants();
    // The provider (a child of the session) is about to delete its view; take it out of the
    // stage first so nothing here points at a dying widget.
    setWebView(nullptr);
  });
  auto *model = session->provider()->participants();
  mParticipantConnections = {
    connect(model, &QAbstractItemModel::rowsInserted, this, &CallPage::syncParticipants),
    connect(model, &QAbstractItemModel::rowsRemoved, this, &CallPage::syncParticipants),
    connect(model, &QAbstractItemModel::modelReset, this, &CallPage::syncParticipants),
    connect(model, &QAbstractItemModel::dataChanged, this, &CallPage::syncParticipants),
    connect(session->provider(), &pcm::video::VideoProvider::screenSharingChanged, this,
            &CallPage::refreshMediaButtons)
  };
  syncParticipants();
  refreshMediaButtons();
  onSessionStateChanged(session->state());
}

void CallPage::setWebView(QWidget *view) {
  const bool wasWeb = !mSavedControlVisibility.isEmpty();
  mWebView = view;
  mVideoStage->setContentWidget(view);
  const bool native = view == nullptr;
  // The web page has its own mic/camera/share/device controls (and Jitsi owns the devices).
  // Visibility is touched only on a mode switch, so a native session keeps whatever the
  // native flow chose, and leaving web mode restores exactly what was there before.
  if (!native && !wasWeb) {
    for (QWidget *widget : {static_cast<QWidget *>(mMicrophoneToggleButton), static_cast<QWidget *>(mCameraToggleButton),
                            static_cast<QWidget *>(mScreenShareButton), static_cast<QWidget *>(mDevicesButton),
                            mLayoutControls.data()})
      if (widget) {
        mSavedControlVisibility.append({widget, !widget->isHidden()});
        widget->hide();
      }
  } else if (native && wasWeb) {
    for (const auto &saved : std::as_const(mSavedControlVisibility))
      if (saved.first)
        saved.first->setVisible(saved.second);
    mSavedControlVisibility.clear();
  }
  if (!native)
    mWaitingBanner->hide();
  mVideoStage->refreshLayout();
}

void CallPage::refreshMediaButtons() {
  if (!mSession)
    return;
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
  if (mScreenShareButton && mSession->provider()) {
    const QSignalBlocker blocker(mScreenShareButton);
    const bool sharing = mSession->provider()->isScreenSharing();
    mScreenShareButton->setChecked(sharing);
    mScreenShareButton->setIcon(pcm::widgets::screenShareIcon(sharing));
    const QString label = sharing ? tr("Stop sharing screen") : tr("Share screen");
    mScreenShareButton->setToolTip(label);
    mScreenShareButton->setAccessibleName(label);
    if (mSharingBanner) {
      mSharingBanner->setVisible(sharing);
      if (mVideoStage) mVideoStage->refreshLayout();
    }
  }
}

void CallPage::showScreenShareMenu() {
  auto *provider = mSession ? mSession->provider() : nullptr;
  if (!provider || !mScreenShareButton)
    return;
  // On Wayland the desktop's own picker chooses the screen or window.
  if (pcm::video::screenCaptureUsesSystemPicker()) {
    provider->startScreenShare({QGuiApplication::primaryScreen(), {}});
    return;
  }
  auto *menu = new QMenu(this);
  menu->setAttribute(Qt::WA_DeleteOnClose);
  menu->setObjectName("screenShareMenu");
  menu->addSection(tr("Screens"));
  for (QScreen *screen : QGuiApplication::screens()) {
    const QSize size = screen->size();
    auto *action = menu->addAction(tr("%1 (%2×%3)").arg(screen->name()).arg(size.width()).arg(size.height()));
    connect(action, &QAction::triggered, this, [this, screen = QPointer<QScreen>(screen)] {
      if (mSession && mSession->provider() && screen)
        mSession->provider()->startScreenShare({screen, {}});
    });
  }
  const auto windows = QWindowCapture::capturableWindows();
  if (!windows.isEmpty()) {
    menu->addSection(tr("Windows"));
    for (const auto &window : windows) {
      auto *action = menu->addAction(window.description());
      connect(action, &QAction::triggered, this, [this, window] {
        if (mSession && mSession->provider())
          mSession->provider()->startScreenShare({{}, window});
      });
    }
  }
  menu->popup(mScreenShareButton->mapToGlobal(QPoint(0, -menu->sizeHint().height())));
}

void CallPage::clearParticipants() {
  for (const auto &connection : mParticipantConnections)
    disconnect(connection);
  mParticipantConnections.clear();
  mVideoStage->setTiles({});
  qDeleteAll(mTiles);
  mTiles.clear();
}

void CallPage::syncParticipants() {
  if (!mSession || !mSession->provider())
    return;
  auto *provider = mSession->provider();
  auto *model = provider->participants();
  QVector<pcm::video::ParticipantTile *> ordered;
  QSet<QString> present;
  for (int row = 0; row < model->rowCount(); ++row) {
    const QString id = model->data(model->index(row), pcm::video::ParticipantModel::IdRole).toString();
    const auto participant = model->participant(id);
    if (!participant)
      continue;
    present.insert(id);
    auto *tile = mTiles.value(id);
    if (!tile) {
      tile = new pcm::video::ParticipantTile(*participant, mVideoStage);
      mTiles.insert(id, tile);
    } else {
      tile->updateParticipant(*participant);
    }
    tile->attachSource(provider->frameSource(id));
    ordered.append(tile);
    // Every participant may share at the same time; each screen gets its own tile.
    const QString screenKey = id + QStringLiteral("#screen");
    if (participant->screenSharing) {
      present.insert(screenKey);
      auto *screenTile = mTiles.value(screenKey);
      const bool newScreen = screenTile == nullptr;
      if (!screenTile) {
        screenTile = new pcm::video::ParticipantTile(*participant, mVideoStage,
                                                     pcm::video::ParticipantTile::Kind::Screen);
        mTiles.insert(screenKey, screenTile);
      } else {
        screenTile->updateParticipant(*participant);
      }
      screenTile->attachSource(provider->screenSource(id));
      if (newScreen) {
        // The most recently started remote screen is what the call is about right now;
        // your own screen is only featured when nobody else shares.
        if (!participant->isLocal || mFeaturedScreenKey.isEmpty()) {
          mFeaturedScreenKey = screenKey;
          mVideoStage->setFeaturedScreen(screenKey);
        }
        connect(screenTile, &pcm::video::ParticipantTile::clicked, this, [this, screenKey] {
          mFeaturedScreenKey = screenKey;
          mVideoStage->setFeaturedScreen(screenKey);
        });
      }
      ordered.append(screenTile);
    }
  }
  // Approved group layout places self-view after remote participants while
  // preserving remote insertion order and every surviving tile's identity.
  std::stable_partition(ordered.begin(), ordered.end(), [](const auto *tile) {
    return !tile->isLocal();
  });
  // Shared screens come first: they are what the call is currently about.
  std::stable_partition(ordered.begin(), ordered.end(), [](const auto *tile) {
    return tile->isScreen();
  });
  // Remove layout references before deleting departed widgets.
  mVideoStage->setTiles(ordered);
  for (auto it = mTiles.begin(); it != mTiles.end();) {
    if (!present.contains(it.key())) {
      if (it.key() == mFeaturedScreenKey) {
        mFeaturedScreenKey.clear();
        mVideoStage->setFeaturedScreen({});
      }
      delete it.value();
      it = mTiles.erase(it);
    } else {
      ++it;
    }
  }
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
    pcm::widgets::fitComboToLongItems(combo);
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
  const auto microphone = provider ? provider->selectedMicrophone() : QAudioDevice{};
  mDeviceMicrophoneCombo = addDeviceRow(QStringLiteral("deviceMicrophoneCombo"), mDeviceManager->microphones(),
                                         microphone.id(), [provider, devices = mDeviceManager](const QByteArray &id) {
                                           if (!provider) {
                                             return;
                                           }
                                           for (const auto &device : devices->microphones()) {
                                             if (device.id() == id) {
                                               provider->switchMicrophone(device);
                                               return;
                                             }
                                           }
                                         });
  {
    const QSignalBlocker blocker(mDeviceMicrophoneCombo);
    mDeviceMicrophoneCombo->setCurrentIndex(
        microphone.isNull() ? -1 : mDeviceMicrophoneCombo->findData(microphone.id()));
  }
  if (provider) {
    connect(provider, &pcm::video::VideoProvider::microphoneChanged, mDeviceMicrophoneCombo,
            [combo = mDeviceMicrophoneCombo](const QAudioDevice &device) {
              const QSignalBlocker blocker(combo);
              combo->setCurrentIndex(device.isNull() ? -1 : combo->findData(device.id()));
            });
  }
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
    if (auto *host = mSidePanel->parentWidget(); host && host->layout()) host->layout()->removeWidget(mSidePanel);
    mSidePanel->setParent(nullptr);
  }
  mSidePanel = panel;
  if (panel) {
    auto *host = mCallEnded ? mEndedSidePanelHost : mSidePanelHost;
    host->layout()->addWidget(panel);
    panel->show();
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
    // Opaque QSS fills (no alpha: overlays a QOpenGLWidget); the :checked
    // state gives the toggle a visible on/off difference.
    mNotesToggleButton->setStyleSheet(QStringLiteral(
        "#notesToggleButton { background-color: rgb(20, 20, 20); border: none; border-radius: 20px; }"
        "#notesToggleButton:hover { background-color: rgb(45, 45, 45); }"
        "#notesToggleButton:checked { background-color: rgb(70, 70, 70); }"));
    connect(mNotesToggleButton, &QToolButton::toggled, this,
            [this](bool checked) { setSidePanelOpen(checked); });
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

void CallPage::openSidePanel() {
  setSidePanelExpandedByDefault(true);
}

void CallPage::setTranscribeButtonVisible(bool visible) {
  if (visible && !mTranscribeButton) {
    mTranscribeButton = new QToolButton(mControlBar);
    mTranscribeButton->setObjectName("transcribeButton");
    mTranscribeButton->setCheckable(true);
    mTranscribeButton->setFixedSize(40, 40);
    mTranscribeButton->setIconSize(QSize(20, 20));
    auto *layout = qobject_cast<QHBoxLayout *>(mControlBar->layout());
    layout->insertWidget(layout->indexOf(mDevicesButton) + 1, mTranscribeButton);
    setTranscribeButtonState(mTranscribing, mTranscribeTooltip);
    connect(mTranscribeButton, &QToolButton::clicked, this, [this]() {
      mTranscribeButton->setChecked(mTranscribing);
      emit transcribeRequested();
    });
  } else if (!visible && mTranscribeButton) {
    delete mTranscribeButton;
    mTranscribeButton = nullptr;
  }
}

void CallPage::setTranscribeButtonState(bool active, const QString &tooltip) {
  mTranscribing = active;
  mTranscribeTooltip = tooltip;
  if (mTranscribeButton) {
    mTranscribeButton->setChecked(active);
    mTranscribeButton->setIcon(pcm::widgets::transcriptIcon(active));
    const QString label = active ? tooltip : tr("Transcribe");
    mTranscribeButton->setToolTip(label);
    mTranscribeButton->setAccessibleName(label);
  }
}

void CallPage::onSessionStateChanged(const pcm::video::VideoSessionState state) {
  using pcm::video::VideoSessionState;
  mWaitingBanner->setVisible(state == VideoSessionState::WaitingForParticipants && !mWebView);
  mReconnectingBanner->setVisible(state == VideoSessionState::Reconnecting);

  // Fullscreen belongs to the connected call screen only; never leave the app stuck in it
  // behind the ended/failed/prejoin screens.
  if (state != VideoSessionState::WaitingForParticipants && state != VideoSessionState::Connected &&
      state != VideoSessionState::Reconnecting) {
    leaveFullscreenIfActive();
  }

  // A web call has no device-check or native connecting screen: the page shows its own
  // pre-join and connecting UI, so its view must be on screen from the first state.
  const bool web = mWebView != nullptr;
  const bool preJoin = state == VideoSessionState::NoMeeting || state == VideoSessionState::Provisioned ||
                       state == VideoSessionState::PrejoinCheck || state == VideoSessionState::Joining;
  if (web && preJoin) {
    mCallEnded = false;
    mConnectedView->setObjectName("connectedView");
    mStack->setCurrentWidget(mConnectedView);
    return;
  }

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
  case VideoSessionState::WaitingForParticipants:
  case VideoSessionState::Connected:
  case VideoSessionState::Reconnecting:
    mCallEnded = false;
    if (mSidePanel && mSidePanel->parentWidget() == mEndedSidePanelHost) {
      mEndedSidePanelHost->layout()->removeWidget(mSidePanel);
      mSidePanelHost->layout()->addWidget(mSidePanel);
      mSidePanel->show();
    }
    mEndedSidePanelHost->hide();
    mConnectedView->setObjectName("connectedView");
    mStack->setCurrentWidget(mConnectedView);
    break;
  case VideoSessionState::Leaving:
    break;
  case VideoSessionState::Ended:
  case VideoSessionState::Failed:
    mCallEnded = true;
    refreshEndedReason();
    // The page is done: take the view off the stage so the native controls come back. It stays
    // alive, owned by the provider, whose leave()/destruction is what stops the page.
    if (mWebView)
      setWebView(nullptr);
    if (mSidePanel && mNotesToggleButton) {
      if (auto *host = mSidePanel->parentWidget(); host && host->layout()) host->layout()->removeWidget(mSidePanel);
      mEndedSidePanelHost->layout()->addWidget(mSidePanel);
      mSidePanel->show(); mEndedSidePanelHost->show();
    }
    mStack->setCurrentWidget(mEndedScreen);
    emit callEnded();
    break;
  }
}
