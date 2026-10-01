#include "call_page.h"
#include "fake_video_provider.h"
#include "device_manager.h"
#include "busy_spinner.h"

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalSpy>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QTextDocument>
#include <gtest/gtest.h>

using pcm::video::test::FakeVideoProvider;
using pcm::video::VideoSession;
using pcm::video::VideoSessionState;

namespace {

// VideoSession::join()/leave() post their internal requestJoin()/
// requestLeave() signals via Qt::QueuedConnection (see video_session.cpp),
// and its QStateMachine settles asynchronously. Firing all of a scenario's
// FakeVideoProvider::simulate*() calls back-to-back right after join()/
// leave(), then pumping once, races the state machine: a provider signal
// emitted before the session has actually reached the state that listens
// for it is silently dropped, and the session gets stuck. Waiting for each
// intermediate state — the same pattern as VideoSessionTest::waitForState in
// video_session_tests.cpp — is what makes this deterministic.
void waitForState(VideoSession &session, QSignalSpy &stateSpy, VideoSessionState target) {
  while (session.state() != target) {
    ASSERT_TRUE(stateSpy.wait(1000)) << "timed out waiting for state " << static_cast<int>(target);
  }
}

// QWidget::resize() alone does not synchronously invoke resizeEvent() unless
// the widget is actually visible (Qt defers the event until the widget's
// ancestor chain is shown). These tests never show() the page, so exercising
// VideoStage::layoutChildren() against a real, non-default size requires
// explicitly delivering the QResizeEvent after resizing.
void resizeAndDeliverEvent(QWidget *widget, const QSize &size) {
  const QSize oldSize = widget->size();
  widget->resize(size);
  QResizeEvent event(widget->size(), oldSize);
  QApplication::sendEvent(widget, &event);
}

// Drives a CallPage's attached session to Connected (as the individual tests
// below do inline) so the connected screen and its floating overlays exist.
void connectSession(VideoSession &session, FakeVideoProvider *provider) {
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);
  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  provider->simulateJoined();
  waitForState(session, stateSpy, VideoSessionState::WaitingForParticipants);
  provider->simulateParticipantJoined({"remote"});
  waitForState(session, stateSpy, VideoSessionState::Connected);
}

// Renders `root` (the video stage, so the result is what the user sees: the
// overlay composited over whatever is behind it) and returns the pixel at
// `point` given in `target`'s coordinates.
QColor grabbedPixel(QWidget *root, QWidget *target, const QPoint &point) {
  const QImage image = root->grab().toImage().convertToFormat(QImage::Format_ARGB32);
  return image.pixelColor(target->mapTo(root, point));
}

} // namespace

TEST(CallPageTest, StartsOnDeviceCheckScreen) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  EXPECT_NE(page.findChild<QWidget *>("deviceCheckWidget"), nullptr);
  EXPECT_EQ(page.findChild<QWidget *>("connectedView"), nullptr);
}

TEST(CallPageTest, GroupStageCreatesEveryIdentityAndRetainsSurvivorOnDeparture) {
  pcm::video::DeviceManager devices;
  CallPage page(&devices);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  provider->simulateParticipantJoined({"local", "Me", "client", true, true, false});
  provider->simulateParticipantJoined({"first", "First"});
  provider->simulateParticipantJoined({"second", "<b>Second</b>"});
  QPointer<QWidget> survivor = page.findChild<QWidget *>("participantTile_second");
  ASSERT_NE(survivor, nullptr);
  EXPECT_NE(page.findChild<QWidget *>("participantTile_local"), nullptr);
  EXPECT_NE(page.findChild<QWidget *>("participantTile_first"), nullptr);
  resizeAndDeliverEvent(page.findChild<QWidget *>("videoStage"), QSize(1280, 720));
  EXPECT_GT(page.findChild<QWidget *>("participantTile_local")->y(), survivor->y());
  provider->simulateParticipantLeft("first");
  EXPECT_EQ(page.findChild<QWidget *>("participantTile_second"), survivor.data());
  EXPECT_EQ(page.findChild<QWidget *>("participantTile_first"), nullptr);
}

TEST(CallPageTest, WaitingStageKeepsLocalPreviewAndLeaveVisible) {
  pcm::video::DeviceManager devices;
  CallPage page(&devices);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  provider->simulateParticipantJoined({"local", "Me", "client", true});
  page.onSessionStateChanged(VideoSessionState::WaitingForParticipants);
  auto *tile = page.findChild<QWidget *>("participantTile_local");
  ASSERT_NE(tile, nullptr);
  EXPECT_TRUE(tile->isVisibleTo(&page));
  EXPECT_TRUE(page.findChild<QPushButton *>("leaveButton")->isVisibleTo(&page));
  auto *waiting = page.findChild<QLabel *>("waitingLabel");
  ASSERT_NE(waiting, nullptr);
  EXPECT_TRUE(waiting->isVisibleTo(&page));
}

TEST(CallPageTest, TwoRemotesAndLocalHaveEqualTilesAndCenteredLastRow) {
  pcm::video::detail::VideoStage stage;
  auto *local = new pcm::video::ParticipantTile({"local", "Me", "", true}, &stage);
  auto *first = new pcm::video::ParticipantTile({"first"}, &stage);
  auto *second = new pcm::video::ParticipantTile({"second"}, &stage);
  stage.setTiles({local, first, second});
  resizeAndDeliverEvent(&stage, QSize(1280, 720));
  EXPECT_EQ(local->size(), first->size());
  EXPECT_EQ(second->size(), first->size());
  EXPECT_EQ(local->parentWidget(), first->parentWidget());
  EXPECT_NEAR(second->geometry().center().x(), second->parentWidget()->rect().center().x(), 1);
  EXPECT_GT(second->y(), first->y());
}

TEST(CallPageTest, OneRemoteAndLocalUsePipAboveControlBar) {
  pcm::video::detail::VideoStage stage;
  auto *local = new pcm::video::ParticipantTile({"local", "Me", "", true}, &stage);
  auto *remote = new pcm::video::ParticipantTile({"remote", QString(64, 'W')}, &stage);
  auto *bar = new QWidget(&stage);
  bar->resize(300, 48);
  stage.setControlBarWidget(bar);
  stage.setTiles({local, remote});
  resizeAndDeliverEvent(&stage, QSize(1280, 720));
  stage.show();
  QApplication::processEvents();
  EXPECT_EQ(local->parentWidget(), &stage);
  EXPECT_LT(local->width(), remote->width());
  EXPECT_LT(local->geometry().bottom(), bar->geometry().top());
  EXPECT_EQ(QRect(remote->mapTo(&stage, QPoint()), remote->size()), stage.rect());
  EXPECT_GE(local->width(), 200);
  auto *name = remote->findChild<QLabel *>("participantName");
  ASSERT_NE(name, nullptr);
  EXPECT_FALSE(QRect(name->mapTo(&stage, QPoint()), name->size()).intersects(bar->geometry()));
  auto *notes = new QWidget(&stage);
  stage.setNotesToggleWidget(notes);
  resizeAndDeliverEvent(&stage, QSize(480, 720));
  QApplication::processEvents();
  const QRect nameGeometry(name->mapTo(&stage, QPoint()), name->size());
  EXPECT_TRUE(stage.rect().contains(nameGeometry));
  EXPECT_FALSE(nameGeometry.intersects(bar->geometry()));
  EXPECT_FALSE(nameGeometry.intersects(notes->geometry()));
}

TEST(CallPageTest, ReturningFromGroupToPipCentersRemoteInItsHost) {
  pcm::video::detail::VideoStage stage;
  QVector<pcm::video::ParticipantTile *> tiles;
  tiles.append(new pcm::video::ParticipantTile({"local", "Me", "", true}, &stage));
  for (int i = 1; i < 10; ++i)
    tiles.append(new pcm::video::ParticipantTile({QString::number(i)}, &stage));
  stage.setTiles(tiles);
  resizeAndDeliverEvent(&stage, QSize(1280, 720));
  stage.setTiles({tiles[0], tiles[1]});
  EXPECT_NEAR(tiles[1]->geometry().center().x(), tiles[1]->parentWidget()->rect().center().x(), 1);
  EXPECT_NEAR(tiles[1]->geometry().center().y(), tiles[1]->parentWidget()->rect().center().y(), 1);
}

TEST(CallPageTest, SixAndTenTilesStayVisibleInsideStageAndClearOfControls) {
  for (int count : {6, 10}) {
    pcm::video::detail::VideoStage stage;
    auto *bar = new QWidget(&stage);
    bar->resize(360, 48);
    stage.setControlBarWidget(bar);
    auto *notes = new QWidget(&stage);
    stage.setNotesToggleWidget(notes);
    QVector<pcm::video::ParticipantTile *> tiles;
    for (int i = 0; i < count; ++i)
      tiles.append(new pcm::video::ParticipantTile({QString::number(i)}, &stage));
    stage.setTiles(tiles);
    for (const QSize size : {QSize(1280, 720), QSize(480, 900)}) {
      resizeAndDeliverEvent(&stage, size);
      for (auto *tile : tiles) {
        const QRect geometry(tile->mapTo(&stage, QPoint()), tile->size());
        EXPECT_TRUE(stage.rect().contains(geometry));
        EXPECT_FALSE(geometry.intersects(bar->geometry()));
        EXPECT_FALSE(geometry.intersects(notes->geometry()));
        EXPECT_TRUE(tile->isVisibleTo(&stage));
        EXPECT_EQ(tile->size(), tiles.first()->size());
      }
    }
  }
}

TEST(CallPageTest, SessionSwapDeletionAndModelResetDestroyOldTiles) {
  pcm::video::DeviceManager devices;
  CallPage page(&devices);
  auto *firstProvider = new FakeVideoProvider();
  VideoSession first(firstProvider);
  page.attachSession(&first);
  firstProvider->simulateParticipantJoined({"first"});
  QPointer<QWidget> old = page.findChild<QWidget *>("participantTile_first");
  ASSERT_NE(old, nullptr);
  auto *secondProvider = new FakeVideoProvider();
  auto second = std::make_unique<VideoSession>(secondProvider);
  page.attachSession(second.get());
  EXPECT_TRUE(old.isNull());
  firstProvider->simulateParticipantJoined({"stale"});
  EXPECT_EQ(page.findChild<QWidget *>("participantTile_stale"), nullptr);
  secondProvider->simulateParticipantJoined({"second"});
  QPointer<QWidget> current = page.findChild<QWidget *>("participantTile_second");
  ASSERT_NE(current, nullptr);
  secondProvider->participants()->clear();
  EXPECT_TRUE(current.isNull());
  secondProvider->simulateParticipantJoined({"second"});
  current = page.findChild<QWidget *>("participantTile_second");
  second.reset();
  EXPECT_TRUE(current.isNull());
  page.attachSession(nullptr);
}

TEST(CallPageTest, DisplayNameIsPlainTextAndCameraOffSurvivesSourceDeletion) {
  pcm::video::DeviceManager devices;
  CallPage page(&devices);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  provider->simulateParticipantJoined({"remote", "<b>Remote</b>", "client", false, true, false});
  auto *tile = page.findChild<QWidget *>("participantTile_remote");
  ASSERT_NE(tile, nullptr);
  auto *label = tile->findChild<QLabel *>("participantName");
  EXPECT_EQ(label->textFormat(), Qt::PlainText);
  EXPECT_EQ(label->text(), "<b>Remote</b>");
  QTextDocument tooltip;
  tooltip.setHtml(label->toolTip());
  EXPECT_EQ(tooltip.toPlainText(), label->text());
  auto *placeholder = tile->findChild<QLabel *>("cameraOffPlaceholder");
  EXPECT_FALSE(placeholder->isHidden());
  delete provider->mSources.take("remote");
  EXPECT_FALSE(placeholder->isHidden());
  provider->simulateParticipantJoined({"remote", "Renamed", "practitioner", false, true, false});
  EXPECT_EQ(page.findChild<QWidget *>("participantTile_remote"), tile);
  EXPECT_EQ(label->text(), "Renamed");
  tooltip.setHtml(label->toolTip());
  EXPECT_EQ(tooltip.toPlainText(), label->text());
}

TEST(CallPageTest, SourceFramesClearAndDestructionUpdatePlaceholderWithoutRecreatingTile) {
  pcm::video::ParticipantTile tile({"remote"});
  auto source = std::make_unique<pcm::video::VideoFrameSource>();
  tile.attachSource(source.get());
  auto *placeholder = tile.findChild<QLabel *>("cameraOffPlaceholder");
  auto *renderer = tile.findChild<QWidget *>("participantRenderer");
  ASSERT_NE(placeholder, nullptr);
  EXPECT_FALSE(placeholder->isHidden());
  QImage frame(16, 9, QImage::Format_RGBA8888);
  frame.fill(Qt::red);
  source->submitFrame(frame);
  QApplication::processEvents();
  EXPECT_TRUE(placeholder->isHidden());
  EXPECT_FALSE(renderer->isHidden());
  source->clear();
  QApplication::processEvents();
  EXPECT_FALSE(placeholder->isHidden());
  source->submitFrame(frame);
  QApplication::processEvents();
  EXPECT_TRUE(placeholder->isHidden());
  source.reset();
  EXPECT_FALSE(placeholder->isHidden());
  EXPECT_TRUE(renderer->isHidden());
}

TEST(CallPageTest, RefusedMediaChangeRestoresProviderStateAndLabels) {
  pcm::video::DeviceManager devices;
  CallPage page(&devices);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  provider->mRejectMediaChanges = true;
  auto *mic = page.findChild<QToolButton *>("microphoneToggleButton");
  auto *camera = page.findChild<QToolButton *>("cameraToggleButton");
  mic->setChecked(false);
  camera->setChecked(false);
  EXPECT_TRUE(mic->isChecked());
  EXPECT_TRUE(camera->isChecked());
  EXPECT_EQ(mic->toolTip(), "Mute microphone");
  EXPECT_EQ(camera->toolTip(), "Turn off camera");
  EXPECT_EQ(provider->mSetMicrophoneEnabledCallCount, 1);
  EXPECT_EQ(provider->mSetCameraEnabledCallCount, 1);
}

TEST(CallPageTest, NotesPanelResizeRelayoutsEveryParticipantWithoutOverlap) {
  pcm::video::DeviceManager devices;
  CallPage page(&devices);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  connectSession(session, provider);
  page.setSidePanelToggleVisible(true);
  auto *panel = new QWidget;
  panel->setFixedWidth(280);
  page.setSidePanelWidget(panel);
  for (int i = 0; i < 6; ++i)
    provider->simulateParticipantJoined({QString::number(i)});
  page.resize(1280, 720);
  page.show();
  QApplication::processEvents();
  auto *stage = page.findChild<QWidget *>("videoStage");
  const int oldWidth = stage->width();
  page.setSidePanelExpandedByDefault(true);
  QApplication::processEvents();
  EXPECT_LT(stage->width(), oldWidth);
  auto *bar = page.findChild<QWidget *>("controlBar");
  for (int i = 0; i < 6; ++i) {
    auto *tile = page.findChild<QWidget *>("participantTile_" + QString::number(i));
    const QRect geometry(tile->mapTo(stage, QPoint()), tile->size());
    EXPECT_TRUE(stage->rect().contains(geometry));
    EXPECT_FALSE(geometry.intersects(bar->geometry()));
  }
}

TEST(CallPageTest, ControlBarIsAChildOfVideoStageNotConnectedView) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);

  auto *micButton = page.findChild<QToolButton *>("microphoneToggleButton");
  ASSERT_NE(micButton, nullptr);
  // VideoStage has no Q_OBJECT macro, so it cannot be used as a
  // findChild<T*>() template argument (compile error: "No Q_OBJECT in the
  // class passed to QObject::findChild"); look it up by its object name
  // instead, exactly as buildConnectedScreen() assigns it.
  auto *videoStage = page.findChild<QWidget *>("videoStage");
  ASSERT_NE(videoStage, nullptr);

  QWidget *ancestor = micButton->parentWidget();
  bool foundVideoStageAncestor = false;
  while (ancestor != nullptr) {
    if (ancestor == videoStage) {
      foundVideoStageAncestor = true;
      break;
    }
    ancestor = ancestor->parentWidget();
  }
  EXPECT_TRUE(foundVideoStageAncestor);
}

TEST(CallPageTest, ConnectedStateShowsConnectedViewWithLeaveButton) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);

  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  provider->simulateJoined();
  waitForState(session, stateSpy, VideoSessionState::WaitingForParticipants);
  provider->simulateParticipantJoined({"remote"});
  waitForState(session, stateSpy, VideoSessionState::Connected);

  EXPECT_NE(page.findChild<QWidget *>("connectedView"), nullptr);
  auto *leaveButton = page.findChild<QPushButton *>("leaveButton");
  ASSERT_NE(leaveButton, nullptr);

  QSignalSpy leaveSpy(&page, &CallPage::leaveRequested);
  leaveButton->click();
  EXPECT_EQ(leaveSpy.count(), 1);
}

TEST(CallPageTest, EndedStateEmitsCallEnded) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);
  QSignalSpy endedSpy(&page, &CallPage::callEnded);

  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  provider->simulateJoined();
  waitForState(session, stateSpy, VideoSessionState::WaitingForParticipants);
  session.leave();
  waitForState(session, stateSpy, VideoSessionState::Leaving);
  provider->simulateLeft();
  waitForState(session, stateSpy, VideoSessionState::Ended);

  EXPECT_EQ(endedSpy.count(), 1);
}

// Fixwave group 1, bug 3: a join failure used to route to the ended screen
// with only a generic "Call ended." label — the reason was dropped.
TEST(CallPageTest, JoinFailureReasonIsShownOnEndedScreen) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);

  auto *reasonLabel = page.findChild<QLabel *>("endedReasonLabel");
  ASSERT_NE(reasonLabel, nullptr);
  EXPECT_TRUE(reasonLabel->isHidden());

  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  provider->simulateJoinFailed("no route to host");
  waitForState(session, stateSpy, VideoSessionState::Failed);

  EXPECT_FALSE(reasonLabel->isHidden());
  EXPECT_EQ(reasonLabel->text(), QStringLiteral("no route to host"));
}

TEST(CallPageTest, ReconnectFailureReasonIsShownOnEndedScreen) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider, std::chrono::milliseconds(50));
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);

  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  provider->simulateJoined();
  waitForState(session, stateSpy, VideoSessionState::WaitingForParticipants);
  provider->simulateParticipantJoined({"remote"});
  waitForState(session, stateSpy, VideoSessionState::Connected);
  provider->simulateReconnecting();
  waitForState(session, stateSpy, VideoSessionState::Reconnecting);
  waitForState(session, stateSpy, VideoSessionState::Failed);

  auto *reasonLabel = page.findChild<QLabel *>("endedReasonLabel");
  ASSERT_NE(reasonLabel, nullptr);
  EXPECT_FALSE(reasonLabel->isHidden());
  EXPECT_EQ(reasonLabel->text(), QStringLiteral("Reconnection timed out."));
}

TEST(CallPageTest, ConnectionLostBeforeJoinedShowsEndedScreenAndPreservesReason) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);
  QSignalSpy endedSpy(&page, &CallPage::callEnded);

  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  auto *connectingLabel = page.findChild<QLabel *>("connectingLabel");
  ASSERT_NE(connectingLabel, nullptr);
  ASSERT_TRUE(connectingLabel->isVisibleTo(&page));
  provider->simulateConnectionLost("room ended before join completed");
  waitForState(session, stateSpy, VideoSessionState::Failed);
  ASSERT_EQ(session.state(), VideoSessionState::Failed);

  auto *reasonLabel = page.findChild<QLabel *>("endedReasonLabel");
  ASSERT_NE(reasonLabel, nullptr);
  EXPECT_TRUE(reasonLabel->isVisibleTo(&page));
  EXPECT_EQ(reasonLabel->text(), QStringLiteral("room ended before join completed"));
  EXPECT_FALSE(connectingLabel->isVisibleTo(&page));
  EXPECT_EQ(endedSpy.count(), 1);

  provider->simulateJoined();
  provider->simulateLeft();
  QCoreApplication::processEvents();
  QCoreApplication::processEvents();
  EXPECT_EQ(session.state(), VideoSessionState::Failed);
  EXPECT_TRUE(reasonLabel->isVisibleTo(&page));
  EXPECT_EQ(endedSpy.count(), 1);
}

TEST(CallPageTest, NormalLeaveAfterEarlierFailureShowsNoStaleReason) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);

  // First session fails with a reason...
  auto *failingProvider = new FakeVideoProvider();
  VideoSession failingSession(failingProvider);
  page.attachSession(&failingSession);
  QSignalSpy failingSpy(&failingSession, &VideoSession::stateChanged);
  failingSession.join("wss://x", "token");
  waitForState(failingSession, failingSpy, VideoSessionState::Joining);
  failingProvider->simulateJoinFailed("no route to host");
  waitForState(failingSession, failingSpy, VideoSessionState::Failed);
  auto *reasonLabel = page.findChild<QLabel *>("endedReasonLabel");
  ASSERT_NE(reasonLabel, nullptr);
  ASSERT_FALSE(reasonLabel->isHidden());

  // ...then a fresh session is attached and left normally: no reason.
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);
  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  provider->simulateJoined();
  waitForState(session, stateSpy, VideoSessionState::WaitingForParticipants);
  session.leave();
  waitForState(session, stateSpy, VideoSessionState::Leaving);
  provider->simulateLeft();
  waitForState(session, stateSpy, VideoSessionState::Ended);

  EXPECT_TRUE(reasonLabel->isHidden());
  EXPECT_TRUE(reasonLabel->text().isEmpty());
}

// Fullscreen is for the call alone: CallPage tells its host to drop the surrounding chrome and
// hides the notes panel itself; leaving fullscreen puts the panel back as the user had it.
TEST(CallPageTest, FullscreenHidesNotesPanelAndAnnouncesTheChange) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  page.setSidePanelToggleVisible(true);
  auto *panel = new QWidget;
  page.setSidePanelWidget(panel);
  page.setSidePanelExpandedByDefault(true);
  // Toggling fullscreen shows the top-level window; drive the session to Connected first so the
  // connected view (not the real-camera device check) is the current page. See the sibling
  // FullscreenToggleButtonHasAccessibleLabelThatUpdatesOnToggle test.
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);
  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  provider->simulateJoined();
  waitForState(session, stateSpy, VideoSessionState::WaitingForParticipants);
  provider->simulateParticipantJoined({"remote"});
  waitForState(session, stateSpy, VideoSessionState::Connected);
  auto *notesToggle = page.findChild<QToolButton *>("notesToggleButton");
  auto *fullscreenButton = page.findChild<QToolButton *>("fullscreenToggleButton");
  ASSERT_NE(notesToggle, nullptr);
  ASSERT_NE(fullscreenButton, nullptr);
  ASSERT_TRUE(notesToggle->isChecked());

  QSignalSpy spy(&page, &CallPage::fullscreenChanged);
  fullscreenButton->setChecked(true);
  ASSERT_EQ(spy.count(), 1);
  EXPECT_TRUE(spy.takeFirst().at(0).toBool());
  EXPECT_TRUE(notesToggle->isHidden());
  EXPECT_FALSE(panel->isVisibleTo(&page));

  fullscreenButton->setChecked(false);
  ASSERT_EQ(spy.count(), 1);
  EXPECT_FALSE(spy.takeFirst().at(0).toBool());
  EXPECT_FALSE(notesToggle->isHidden());
  EXPECT_TRUE(panel->isVisibleTo(&page)); // the user had it open
}

TEST(CallPageTest, VideoStagePositionsControlBarCenteredAtBottom) {
  pcm::video::detail::VideoStage stage;
  auto *controlBar = new QWidget(&stage);
  controlBar->resize(200, 48);
  stage.setControlBarWidget(controlBar);
  resizeAndDeliverEvent(&stage, QSize(640, 360));

  EXPECT_EQ(controlBar->parentWidget(), &stage);
  EXPECT_EQ(controlBar->geometry().center().x(), stage.rect().center().x());
  EXPECT_EQ(controlBar->geometry().bottom(), stage.height() - 12 - 1);
}

// The "waiting for others" pill floats centred just above the control bar, over the video, rather
// than taking a strip of its own below it.
TEST(CallPageTest, VideoStagePositionsWaitingBannerCenteredAboveTheControlBar) {
  pcm::video::detail::VideoStage stage;
  auto *controlBar = new QWidget;
  controlBar->resize(200, 48);
  stage.setControlBarWidget(controlBar);
  auto *banner = new QLabel(QStringLiteral("Waiting for others to join"));
  stage.setWaitingBanner(banner);
  resizeAndDeliverEvent(&stage, QSize(640, 360));

  EXPECT_EQ(banner->parentWidget(), &stage);
  EXPECT_EQ(banner->geometry().center().x(), stage.rect().center().x());
  EXPECT_LT(banner->geometry().bottom(), controlBar->geometry().top());
  EXPECT_GE(controlBar->geometry().top() - banner->geometry().bottom(), 8);
}

TEST(CallPageTest, VideoStagePositionsNotesToggleInTopRightCorner) {
  pcm::video::detail::VideoStage stage;
  auto *notesToggle = new QWidget(&stage);
  stage.setNotesToggleWidget(notesToggle);
  resizeAndDeliverEvent(&stage, QSize(640, 360));

  EXPECT_EQ(notesToggle->parentWidget(), &stage);
  EXPECT_EQ(notesToggle->geometry(), QRect(640 - 12 - 40, 12, 40, 40));
}

TEST(CallPageTest, SwappingControlBarHandsPreviousOneBackUnparented) {
  pcm::video::detail::VideoStage stage;
  auto *firstBar = new QWidget(&stage);
  stage.setControlBarWidget(firstBar);
  auto *secondBar = new QWidget(&stage);
  stage.setControlBarWidget(secondBar);

  EXPECT_EQ(firstBar->parentWidget(), nullptr);
  EXPECT_EQ(secondBar->parentWidget(), &stage);
  delete firstBar;
}

// The floating bar and notes button sit over a QOpenGLWidget video renderer,
// so their fill must be fully opaque. Once a QSS rule matches, QStyleSheetStyle
// owns background painting and a QPalette + autoFillBackground fill is never
// painted, so these tests render the widgets and check real pixels.
TEST(CallPageTest, ControlBarPaintsAnOpaqueDarkBackground) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  connectSession(session, provider);

  auto *videoStage = page.findChild<QWidget *>("videoStage");
  ASSERT_NE(videoStage, nullptr);
  page.show();
  resizeAndDeliverEvent(videoStage, QSize(1280, 720));
  auto *controlBar = page.findChild<QWidget *>("controlBar");
  ASSERT_NE(controlBar, nullptr);
  ASSERT_EQ(controlBar->height(), 48);

  // x=8 is left of the 12px layout margin (no child button there) and inside
  // the pill's rounded end (24px radius, so the circle centre is at x=24).
  const QColor inside = grabbedPixel(videoStage, controlBar, QPoint(8, controlBar->height() / 2));
  EXPECT_EQ(inside.alpha(), 255);
  EXPECT_EQ(inside.red(), 20);
  EXPECT_EQ(inside.green(), 20);
  EXPECT_EQ(inside.blue(), 20);
}

TEST(CallPageTest, NotesToggleButtonPaintsAnOpaqueBackgroundThatDiffersWhenChecked) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  page.setSidePanelToggleVisible(true);
  connectSession(session, provider);

  auto *videoStage = page.findChild<QWidget *>("videoStage");
  ASSERT_NE(videoStage, nullptr);
  page.show();
  resizeAndDeliverEvent(videoStage, QSize(1280, 720));
  auto *notesToggle = page.findChild<QToolButton *>("notesToggleButton");
  ASSERT_NE(notesToggle, nullptr);
  ASSERT_EQ(notesToggle->size(), QSize(40, 40));
  notesToggle->setChecked(false);

  // x=4 is left of the 24px icon and still inside the 20px-radius disc.
  const QPoint sample(4, notesToggle->height() / 2);
  const QColor unchecked = grabbedPixel(videoStage, notesToggle, sample);
  EXPECT_EQ(unchecked.alpha(), 255);
  EXPECT_EQ(unchecked.red(), 20);
  EXPECT_EQ(unchecked.green(), 20);
  EXPECT_EQ(unchecked.blue(), 20);

  notesToggle->setChecked(true);
  const QColor checked = grabbedPixel(videoStage, notesToggle, sample);
  EXPECT_EQ(checked.alpha(), 255);
  EXPECT_NE(checked, unchecked);
}

TEST(CallPageTest, ConnectionLostReasonIsShownOnEndedScreen) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);

  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  provider->simulateJoined();
  waitForState(session, stateSpy, VideoSessionState::WaitingForParticipants);
  provider->simulateParticipantJoined({"remote"});
  waitForState(session, stateSpy, VideoSessionState::Connected);

  provider->simulateConnectionLost("room ended");
  waitForState(session, stateSpy, VideoSessionState::Failed);

  auto *reasonLabel = page.findChild<QLabel *>("endedReasonLabel");
  ASSERT_NE(reasonLabel, nullptr);
  EXPECT_FALSE(reasonLabel->isHidden());
  EXPECT_EQ(reasonLabel->text(), QStringLiteral("room ended"));
}

// Fixwave group 5, bug 1: mediaError() (a local camera/mic/publish problem)
// used to have no UI surface at all — a broken camera/mic failed silently.
// This proves the new banner shows the reason WITHOUT switching away from
// the connected screen and WITHOUT touching the ended-screen mechanism,
// since the call itself keeps going.
TEST(CallPageTest, MediaErrorShowsBannerWithoutEndingCallOrTouchingEndedReason) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);

  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  provider->simulateJoined();
  waitForState(session, stateSpy, VideoSessionState::WaitingForParticipants);
  provider->simulateParticipantJoined({"remote"});
  waitForState(session, stateSpy, VideoSessionState::Connected);

  auto *banner = page.findChild<QLabel *>("mediaErrorBanner");
  ASSERT_NE(banner, nullptr);
  EXPECT_TRUE(banner->isHidden());

  provider->simulateMediaError("camera unplugged");

  // Observable immediately, before any auto-hide timer could possibly fire.
  EXPECT_FALSE(banner->isHidden());
  EXPECT_EQ(banner->text(), QStringLiteral("camera unplugged"));
  EXPECT_EQ(session.state(), VideoSessionState::Connected);
  auto *reasonLabel = page.findChild<QLabel *>("endedReasonLabel");
  ASSERT_NE(reasonLabel, nullptr);
  EXPECT_TRUE(reasonLabel->isHidden());
  EXPECT_TRUE(reasonLabel->text().isEmpty());
}

// Fixwave userfeedback group A, bug 2: VideoSessionState::Joining and
// VideoSessionState::WaitingForParticipants both used to route to mConnectingScreen
// with the exact same static "Connecting..." text, leaving a user who had
// already joined and was waiting on the other participant indistinguishable
// from someone still connecting.
TEST(CallPageTest, WaitingForParticipantsShowsDistinctMessageFromJoining) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);

  auto *connectingLabel = page.findChild<QLabel *>("connectingLabel");
  ASSERT_NE(connectingLabel, nullptr);

  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  EXPECT_EQ(connectingLabel->text(), QStringLiteral("Connecting..."));

  provider->simulateJoined();
  waitForState(session, stateSpy, VideoSessionState::WaitingForParticipants);
  EXPECT_EQ(page.findChild<QLabel *>("waitingLabel")->text(), QStringLiteral("Waiting for others to join"));
  EXPECT_TRUE(page.findChild<QLabel *>("waitingLabel")->isVisibleTo(&page));
}

// The notes panel sits next to the video in a splitter, so the user can drag it narrower (or
// wider) during a call; it can't be collapsed to nothing or squeezed below a usable width.
TEST(CallPageTest, NotesPanelIsResizableViaASplitterWithAUsableMinimumWidth) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *splitter = page.findChild<QSplitter *>("videoSplitter");
  auto *stage = page.findChild<QWidget *>("videoStage");
  auto *host = page.findChild<QWidget *>("sidePanelHost");
  ASSERT_NE(splitter, nullptr);
  ASSERT_NE(stage, nullptr);
  ASSERT_NE(host, nullptr);
  EXPECT_EQ(splitter->orientation(), Qt::Horizontal);
  EXPECT_EQ(splitter->indexOf(stage), 0);
  EXPECT_EQ(splitter->indexOf(host), 1);
  EXPECT_FALSE(splitter->childrenCollapsible());
  EXPECT_GE(host->minimumWidth(), 160);
}

TEST(CallPageTest, SidePanelToggleHiddenByDefaultUntilMadeVisible) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  EXPECT_EQ(page.findChild<QToolButton *>("notesToggleButton"), nullptr);

  page.setSidePanelToggleVisible(true);
  EXPECT_NE(page.findChild<QToolButton *>("notesToggleButton"), nullptr);
}

TEST(CallPageTest, ConnectingScreenShowsCenteredSpinnerAndHeadline) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);

  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);

  auto *connectingLabel = page.findChild<QLabel *>("connectingLabel");
  ASSERT_NE(connectingLabel, nullptr);
  EXPECT_EQ(connectingLabel->alignment() & Qt::AlignHCenter, Qt::AlignHCenter);

  auto *spinner = page.findChild<pcm::widgets::BusySpinner *>("connectingSpinner");
  ASSERT_NE(spinner, nullptr);
  EXPECT_TRUE(spinner->isVisibleTo(spinner->parentWidget()));
}

TEST(CallPageTest, ReconnectingBannerHasASpinner) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *spinner = page.findChild<pcm::widgets::BusySpinner *>("reconnectingSpinner");
  ASSERT_NE(spinner, nullptr);
}

TEST(CallPageTest, MicrophoneToggleButtonCallsProviderAndStartsEnabled) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);

  auto *micButton = page.findChild<QToolButton *>("microphoneToggleButton");
  ASSERT_NE(micButton, nullptr);
  EXPECT_TRUE(micButton->isChecked());

  micButton->setChecked(false);
  EXPECT_EQ(provider->mSetMicrophoneEnabledCallCount, 1);
  EXPECT_FALSE(provider->isMicrophoneEnabled());
}

TEST(CallPageTest, MicrophoneToggleButtonHasAccessibleLabelThatUpdatesOnToggle) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);

  auto *micButton = page.findChild<QToolButton *>("microphoneToggleButton");
  ASSERT_NE(micButton, nullptr);
  EXPECT_FALSE(micButton->toolTip().isEmpty());
  EXPECT_FALSE(micButton->accessibleName().isEmpty());
  const QString mutedLabel = micButton->toolTip();

  micButton->setChecked(false);
  EXPECT_NE(micButton->toolTip(), mutedLabel);
  EXPECT_EQ(micButton->toolTip(), CallPage::tr("Unmute microphone"));
  EXPECT_EQ(micButton->accessibleName(), micButton->toolTip());
}

TEST(CallPageTest, CameraToggleButtonCallsProvider) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);

  auto *cameraButton = page.findChild<QToolButton *>("cameraToggleButton");
  ASSERT_NE(cameraButton, nullptr);
  EXPECT_TRUE(cameraButton->isChecked());

  cameraButton->setChecked(false);
  EXPECT_EQ(provider->mSetCameraEnabledCallCount, 1);
  EXPECT_FALSE(provider->isCameraEnabled());
}

TEST(CallPageTest, CameraToggleButtonHasAccessibleLabelThatUpdatesOnToggle) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);

  auto *cameraButton = page.findChild<QToolButton *>("cameraToggleButton");
  ASSERT_NE(cameraButton, nullptr);
  EXPECT_FALSE(cameraButton->toolTip().isEmpty());
  EXPECT_FALSE(cameraButton->accessibleName().isEmpty());
  const QString onLabel = cameraButton->toolTip();

  cameraButton->setChecked(false);
  EXPECT_NE(cameraButton->toolTip(), onLabel);
  EXPECT_EQ(cameraButton->toolTip(), CallPage::tr("Turn on camera"));
  EXPECT_EQ(cameraButton->accessibleName(), cameraButton->toolTip());
}

TEST(CallPageTest, FullscreenToggleButtonExistsAndIsCheckable) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *fullscreenButton = page.findChild<QToolButton *>("fullscreenToggleButton");
  ASSERT_NE(fullscreenButton, nullptr);
  EXPECT_TRUE(fullscreenButton->isCheckable());
  EXPECT_FALSE(fullscreenButton->isChecked());
}

TEST(CallPageTest, FullscreenToggleButtonHasAccessibleLabelThatUpdatesOnToggle) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  // Toggling this button also calls window()->showFullScreen(), which shows
  // the CallPage's top-level window and, with it, whichever QStackedWidget
  // page is currently current. Left on the default device-check screen, that
  // cascade fires DeviceCheckWidget::showEvent() and starts a *real* camera
  // preview adapter, which throws in this LiveKit-less test environment.
  // Driving the session to Connected first makes connectedView (backed by
  // FakeVideoProvider) the current page instead, so no real device is
  // touched.
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy stateSpy(&session, &VideoSession::stateChanged);
  session.join("wss://x", "token");
  waitForState(session, stateSpy, VideoSessionState::Joining);
  provider->simulateJoined();
  waitForState(session, stateSpy, VideoSessionState::WaitingForParticipants);
  provider->simulateParticipantJoined({"remote"});
  waitForState(session, stateSpy, VideoSessionState::Connected);

  auto *fullscreenButton = page.findChild<QToolButton *>("fullscreenToggleButton");
  ASSERT_NE(fullscreenButton, nullptr);
  EXPECT_FALSE(fullscreenButton->toolTip().isEmpty());
  EXPECT_FALSE(fullscreenButton->accessibleName().isEmpty());
  const QString windowedLabel = fullscreenButton->toolTip();

  fullscreenButton->setChecked(true);
  EXPECT_NE(fullscreenButton->toolTip(), windowedLabel);
  EXPECT_EQ(fullscreenButton->toolTip(), CallPage::tr("Exit fullscreen"));
  EXPECT_EQ(fullscreenButton->accessibleName(), fullscreenButton->toolTip());

  // Leave fullscreen so later tests in the same process don't inherit a
  // fullscreen top-level window.
  fullscreenButton->setChecked(false);
}

TEST(CallPageTest, DevicesButtonHasAccessibleLabel) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *devicesButton = page.findChild<QToolButton *>("devicesButton");
  ASSERT_NE(devicesButton, nullptr);
  EXPECT_FALSE(devicesButton->toolTip().isEmpty());
  EXPECT_EQ(devicesButton->accessibleName(), devicesButton->toolTip());
}

TEST(CallPageTest, DevicesButtonOpensPopoverWithThreeCombos) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *devicesButton = page.findChild<QToolButton *>("devicesButton");
  ASSERT_NE(devicesButton, nullptr);

  devicesButton->click();

  auto *cameraCombo = page.findChild<QComboBox *>("deviceCameraCombo");
  auto *microphoneCombo = page.findChild<QComboBox *>("deviceMicrophoneCombo");
  auto *speakerCombo = page.findChild<QComboBox *>("deviceSpeakerCombo");
  EXPECT_NE(cameraCombo, nullptr);
  EXPECT_NE(microphoneCombo, nullptr);
  EXPECT_NE(speakerCombo, nullptr);
}

TEST(CallPageTest, SelectingADeviceCallsSwitchOnTheAttachedProvider) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);

  auto *devicesButton = page.findChild<QToolButton *>("devicesButton");
  ASSERT_NE(devicesButton, nullptr);
  devicesButton->click();

  auto *microphoneCombo = page.findChild<QComboBox *>("deviceMicrophoneCombo");
  ASSERT_NE(microphoneCombo, nullptr);
  if (microphoneCombo->count() > 1) {
    microphoneCombo->setCurrentIndex(microphoneCombo->currentIndex() == 0 ? 1 : 0);
    EXPECT_EQ(provider->mSwitchMicrophoneCallCount, 1);
  }
}

TEST(CallPageTest, SetSidePanelExpandedByDefaultChecksTheNotesToggle) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  page.setSidePanelToggleVisible(true);
  auto *toggle = page.findChild<QToolButton *>("notesToggleButton");
  ASSERT_NE(toggle, nullptr);
  EXPECT_FALSE(toggle->isChecked());

  page.setSidePanelExpandedByDefault(true);
  EXPECT_TRUE(toggle->isChecked());
}

TEST(CallPageTest, SetSidePanelExpandedByDefaultIsANoOpWithoutATotoggleYet) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  // No crash when called before setSidePanelToggleVisible(true) (client
  // mode, or before Application's eventKnownForCurrentCall handler ever
  // fires).
  page.setSidePanelExpandedByDefault(true);
  EXPECT_EQ(page.findChild<QToolButton *>("notesToggleButton"), nullptr);
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
