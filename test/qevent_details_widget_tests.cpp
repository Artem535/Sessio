#include "qevent_details_widget.h"
#include "event_item.h"
#include "fake_token_backend_server.h"
#include "meeting_coordinator.h"

#include <oclero/qlementine/widgets/SegmentedControl.hpp>

#include <QAbstractButton>
#include <QApplication>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeEdit>
#include <QTimeZone>
#include <QTimer>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <vector>

// QEventDetailsWidget is exercised against a real MeetingCoordinator: with an
// empty base URL its LiveKit provider fails asynchronously (as in
// meeting_coordinator_tests.cpp), and pointed at FakeTokenBackendServer it
// really POSTs /v1/meetings and /v1/meetings/{ref}/invalidate, so the tests
// cover the actual async round-trip the apply has to wait for.
//
// Asynchronous waits use ASSERT_TRUE(QTest::qWaitFor(...)) / QSignalSpy::wait,
// never QtTest's QTRY_* macros (see calls_page_tests.cpp for why).

using pcm::meeting::MeetingCoordinator;
using pcm::meeting::ProviderKind;

namespace {

constexpr auto kCreateResponse = R"({
  "meetingRef": "ref-new", "invitationUrl": "https://x/code-new", "passcode": "123456",
  "scheduledStart": "2026-10-01T10:00:00Z", "scheduledEnd": "2026-10-01T11:00:00Z"
})";

QDateTime at(const int hour, const int minute) {
  return QDateTime(QDate(2026, 10, 1), QTime(hour, minute), QTimeZone::systemTimeZone());
}

// QEventDetailsWidget reports errors through modal QMessageBoxes, which would
// block the test forever. This closes each one as soon as it is shown and
// remembers its text.
class MessageBoxCloser {
public:
  MessageBoxCloser() {
    QObject::connect(&mTimer, &QTimer::timeout, [this]() {
      for (auto *widget : QApplication::topLevelWidgets()) {
        auto *box = qobject_cast<QMessageBox *>(widget);
        if (box && box->isVisible()) {
          texts.append(box->text());
          box->done(QMessageBox::Ok);
        }
      }
    });
    mTimer.start(10);
  }

  QStringList texts;

private:
  QTimer mTimer;
};

struct SavedMeeting {
  std::optional<ProviderKind> kind;
  QString meetingRef;
  std::optional<QString> invitationState;
  qsizetype backendRequestsAtSave = 0;
};

class EventDetailsFixture {
public:
  explicit EventDetailsFixture(MeetingCoordinator *coordinator,
                               const FakeTokenBackendServer *server = nullptr) {
    widget.setMeetingCoordinator(coordinator);
    apply = widget.findChild<QDialogButtonBox *>("mButtonBox")->button(QDialogButtonBox::Apply);
    online = widget.findChild<QAbstractButton *>("onlineSessionSwitch");
    // SegmentedControl has no Q_OBJECT of its own, so findChild cannot target
    // it directly; the objectName is unique to the provider control.
    provider = static_cast<oclero::qlementine::SegmentedControl *>(
        widget.findChild<QWidget *>("providerKindControl"));
    title = widget.findChild<QLineEdit *>("mTitle");
    timeTo = widget.findChild<QTimeEdit *>("mTimeTo");
    openMeeting = widget.findChild<QPushButton *>("openMeetingButton");
    copyLink = widget.findChild<QPushButton *>("copyMeetingUrlButton");
    copyInvite = widget.findChild<QPushButton *>("copyMeetingInviteButton");

    // Stand-in for QEventInfoPage::onEventSaved: record what would be written
    // to the database, and give a new event its database id.
    QObject::connect(&widget, &QEventDetailsWidget::provideEventSave, &widget,
                     [this, server](QEventItem *event) {
                       ASSERT_NE(event, nullptr);
                       saves.push_back({event->providerKind(), event->meetingRef(),
                                        event->invitationState(),
                                        server ? server->requestPaths.size() : 0});
                       if (event->getId() <= 0) {
                         event->setId(101);
                       }
                     });
  }

  [[nodiscard]] bool allFound() const {
    return apply && online && provider && title && timeTo && openMeeting && copyLink &&
           copyInvite;
  }

  QEventDetailsWidget widget;
  QPushButton *apply = nullptr;
  QAbstractButton *online = nullptr;
  oclero::qlementine::SegmentedControl *provider = nullptr;
  QLineEdit *title = nullptr;
  QTimeEdit *timeTo = nullptr;
  QPushButton *openMeeting = nullptr;
  QPushButton *copyLink = nullptr;
  QPushButton *copyInvite = nullptr;
  std::vector<SavedMeeting> saves;
};

std::unique_ptr<QEventItem> makeLiveKitEvent(const QString &meetingRef) {
  auto event = std::make_unique<QEventItem>(7, QStringLiteral("Session"), at(10, 0), at(10, 50));
  event->setOnline(true);
  event->setProviderKind(ProviderKind::LiveKit);
  event->setMeetingRef(meetingRef);
  event->setInvitationState(QStringLiteral("https://x/code-old|000000"));
  return event;
}

} // namespace

TEST(QEventDetailsWidgetTest, NewLiveKitEventIsNotSavedUntilCreateCompletesAndNeverWhenItFails) {
  MeetingCoordinator coordinator("", "");
  EventDetailsFixture form(&coordinator);
  ASSERT_TRUE(form.allFound());
  form.widget.startCreatingNewEvent(QDate(2026, 10, 1), QTime(10, 0), 50);
  form.online->setChecked(true);
  form.provider->setCurrentIndex(1);
  QSignalSpy failedSpy(&coordinator, &MeetingCoordinator::meetingCreateFailed);
  QSignalSpy acceptSpy(&form.widget, &QEventDetailsWidget::provideDialogAccept);
  MessageBoxCloser closer;

  form.apply->click();

  EXPECT_TRUE(form.saves.empty());
  EXPECT_FALSE(form.apply->isEnabled());

  ASSERT_TRUE(failedSpy.wait(2000));
  EXPECT_TRUE(form.saves.empty());
  EXPECT_EQ(acceptSpy.count(), 0);
  EXPECT_TRUE(form.apply->isEnabled());
  EXPECT_TRUE(form.widget.isInEditMode());
  EXPECT_TRUE(form.widget.isCreatingNewEvent());
  ASSERT_NE(form.widget.currentEvent(), nullptr);
  EXPECT_TRUE(form.widget.currentEvent()->meetingRef().isEmpty());
  ASSERT_EQ(closer.texts.size(), 1);
  EXPECT_TRUE(closer.texts.first().startsWith(QStringLiteral("Failed to create the LiveKit meeting")));
}

TEST(QEventDetailsWidgetTest, NewLiveKitEventIsSavedWithCreatedMeetingOnceCreateSucceeds) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, kCreateResponse);
  MeetingCoordinator coordinator(server.baseUrl().toString(), "bearer-secret");
  EventDetailsFixture form(&coordinator, &server);
  ASSERT_TRUE(form.allFound());
  form.widget.startCreatingNewEvent(QDate(2026, 10, 1), QTime(10, 0), 60);
  form.online->setChecked(true);
  form.provider->setCurrentIndex(1);
  QSignalSpy acceptSpy(&form.widget, &QEventDetailsWidget::provideDialogAccept);

  form.apply->click();
  EXPECT_TRUE(form.saves.empty());
  // A second Apply while the create is in flight must not start another one.
  QMetaObject::invokeMethod(&form.widget, "onApplyClicked");

  ASSERT_TRUE(QTest::qWaitFor([&form]() { return !form.saves.empty(); }, 3000));
  ASSERT_EQ(form.saves.size(), 1u);
  EXPECT_EQ(form.saves.front().kind, ProviderKind::LiveKit);
  EXPECT_EQ(form.saves.front().meetingRef, QStringLiteral("ref-new"));
  EXPECT_EQ(form.saves.front().invitationState, QStringLiteral("https://x/code-new|123456"));
  EXPECT_EQ(acceptSpy.count(), 1);
  EXPECT_FALSE(form.widget.isInEditMode());
  EXPECT_EQ(form.widget.currentEvent(), nullptr);

  QTest::qWait(100);
  EXPECT_EQ(server.requestPaths, QStringList{QStringLiteral("/v1/meetings")});
}

TEST(QEventDetailsWidgetTest, ReapplyingLiveKitEventWithUnchangedScheduleKeepsItsMeeting) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, kCreateResponse);
  MeetingCoordinator coordinator(server.baseUrl().toString(), "bearer-secret");
  EventDetailsFixture form(&coordinator, &server);
  ASSERT_TRUE(form.allFound());
  const auto event = makeLiveKitEvent(QStringLiteral("ref-old"));
  form.widget.startEditingEvent(event.get());
  form.title->setText(QStringLiteral("Renamed session"));

  form.apply->click();

  // Nothing meeting-relevant changed, so the save is immediate and keeps the
  // meeting (and the invitation the client already has).
  ASSERT_EQ(form.saves.size(), 1u);
  EXPECT_EQ(form.saves.front().kind, ProviderKind::LiveKit);
  EXPECT_EQ(form.saves.front().meetingRef, QStringLiteral("ref-old"));
  EXPECT_EQ(form.saves.front().invitationState, QStringLiteral("https://x/code-old|000000"));
  EXPECT_EQ(event->getTitle(), QStringLiteral("Renamed session"));
  QTest::qWait(100);
  EXPECT_TRUE(server.requestPaths.isEmpty());
}

TEST(QEventDetailsWidgetTest, RescheduledLiveKitEventReplacesMeetingAndInvalidatesOldOneAfterSave) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, kCreateResponse);
  MeetingCoordinator coordinator(server.baseUrl().toString(), "bearer-secret");
  EventDetailsFixture form(&coordinator, &server);
  ASSERT_TRUE(form.allFound());
  const auto event = makeLiveKitEvent(QStringLiteral("ref-old"));
  form.widget.startEditingEvent(event.get());
  form.timeTo->setTime(QTime(11, 0));
  QSignalSpy canceledSpy(&coordinator, &MeetingCoordinator::meetingCanceled);

  form.apply->click();
  EXPECT_TRUE(form.saves.empty());

  ASSERT_TRUE(QTest::qWaitFor([&form]() { return !form.saves.empty(); }, 3000));
  ASSERT_EQ(form.saves.size(), 1u);
  EXPECT_EQ(form.saves.front().kind, ProviderKind::LiveKit);
  EXPECT_EQ(form.saves.front().meetingRef, QStringLiteral("ref-new"));
  // The old meeting is only invalidated after the replacement was saved.
  EXPECT_EQ(form.saves.front().backendRequestsAtSave, 1);

  ASSERT_TRUE(QTest::qWaitFor([&canceledSpy]() { return canceledSpy.count() == 1; }, 3000));
  EXPECT_EQ(server.requestPaths,
            (QStringList{QStringLiteral("/v1/meetings"),
                         QStringLiteral("/v1/meetings/ref-old/invalidate")}));
  EXPECT_EQ(event->meetingRef(), QStringLiteral("ref-new"));
  EXPECT_FALSE(form.widget.isInEditMode());
  // The freshly created meeting can be opened without reloading the event.
  EXPECT_TRUE(form.openMeeting->isEnabled());
}

TEST(QEventDetailsWidgetTest, FailedReplacementKeepsOldMeetingAndDoesNotInvalidateIt) {
  FakeTokenBackendServer server;
  server.setNextResponse(500, R"({"error": "backend_down"})");
  MeetingCoordinator coordinator(server.baseUrl().toString(), "bearer-secret");
  EventDetailsFixture form(&coordinator, &server);
  ASSERT_TRUE(form.allFound());
  const auto event = makeLiveKitEvent(QStringLiteral("ref-old"));
  form.widget.startEditingEvent(event.get());
  form.timeTo->setTime(QTime(11, 0));
  QSignalSpy failedSpy(&coordinator, &MeetingCoordinator::meetingCreateFailed);
  MessageBoxCloser closer;

  form.apply->click();

  ASSERT_TRUE(failedSpy.wait(3000));
  EXPECT_TRUE(form.saves.empty());
  EXPECT_TRUE(form.widget.isInEditMode());
  EXPECT_TRUE(form.apply->isEnabled());
  EXPECT_EQ(event->meetingRef(), QStringLiteral("ref-old"));
  ASSERT_EQ(closer.texts.size(), 1);
  EXPECT_TRUE(closer.texts.first().contains(QStringLiteral("backend_down")));
  QTest::qWait(100);
  EXPECT_EQ(server.requestPaths, QStringList{QStringLiteral("/v1/meetings")});
}

TEST(QEventDetailsWidgetTest, LiveKitEventWithMeetingRefEnablesOpenAndDisablesCopyButtons) {
  MeetingCoordinator coordinator("", "");
  EventDetailsFixture form(&coordinator);
  ASSERT_TRUE(form.allFound());
  const auto event = makeLiveKitEvent(QStringLiteral("ref-old"));

  form.widget.loadEvent(event.get());

  EXPECT_TRUE(form.openMeeting->isEnabled());
  EXPECT_FALSE(form.copyLink->isEnabled());
  EXPECT_FALSE(form.copyInvite->isEnabled());
}

TEST(QEventDetailsWidgetTest, LiveKitEventWithoutMeetingRefDisablesAllMeetingButtons) {
  MeetingCoordinator coordinator("", "");
  EventDetailsFixture form(&coordinator);
  ASSERT_TRUE(form.allFound());
  const auto event = makeLiveKitEvent(QString{});

  form.widget.loadEvent(event.get());

  EXPECT_FALSE(form.openMeeting->isEnabled());
  EXPECT_FALSE(form.copyLink->isEnabled());
  EXPECT_FALSE(form.copyInvite->isEnabled());
}

TEST(QEventDetailsWidgetTest, ExternalUrlEventWithValidUrlEnablesAllMeetingButtons) {
  MeetingCoordinator coordinator("", "");
  EventDetailsFixture form(&coordinator);
  ASSERT_TRUE(form.allFound());
  QEventItem event(8, QStringLiteral("Session"), at(10, 0), at(10, 50));
  event.setOnline(true);
  event.setProviderKind(ProviderKind::ExternalUrl);
  event.setMeetingRef(QStringLiteral("https://meet.example.test/room-1"));
  event.setMeetingUrl(QStringLiteral("https://meet.example.test/room-1"));

  form.widget.loadEvent(&event);

  EXPECT_TRUE(form.openMeeting->isEnabled());
  EXPECT_TRUE(form.copyLink->isEnabled());
  EXPECT_TRUE(form.copyInvite->isEnabled());
}

TEST(QEventDetailsWidgetTest, OpenOnLiveKitEventRequestsNativeJoinByMeetingRef) {
  MeetingCoordinator coordinator("", "");
  EventDetailsFixture form(&coordinator);
  ASSERT_TRUE(form.allFound());
  const auto event = makeLiveKitEvent(QStringLiteral("ref-old"));
  form.widget.loadEvent(event.get());
  QSignalSpy openSpy(&form.widget, &QEventDetailsWidget::openLiveKitMeetingRequested);

  form.openMeeting->click();

  ASSERT_EQ(openSpy.count(), 1);
  EXPECT_EQ(openSpy.at(0).at(0).toString(), QStringLiteral("ref-old"));
}

int main(int argc, char **argv) {
  // QEventDetailsWidget reads app settings (QSettings) while building its
  // form; keep this test away from the developer's real Sessio settings and
  // off the real display.
  QTemporaryDir isolatedHome;
  qputenv("XDG_CONFIG_HOME", isolatedHome.path().toUtf8());
  qputenv("HOME", isolatedHome.path().toUtf8());
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QStandardPaths::setTestModeEnabled(true);
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
