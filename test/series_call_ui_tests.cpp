#include "series_call_fixture.h"

#include "event_item.h"
#include "qevent_details_widget.h"

#include <oclero/qlementine/widgets/SegmentedControl.hpp>

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimeEdit>
#include <QTimer>

using namespace series_call_test;
using pcm::meeting::ProviderKind;

namespace {

QDateTime at(const int hour, const int minute) {
  return QDateTime(QDate(2026, 10, 27), QTime(hour, minute), QTimeZone::systemTimeZone());
}

struct SavedEvent {
  std::optional<ProviderKind> kind;
  QString meetingRef;
  QString meetingUrl;
  std::optional<QString> invitationState;
};

// The event form wired the way QEventInfoPage uses it, minus the database save.
struct Form {
  explicit Form(MeetingCoordinator *coordinator) {
    widget.setMeetingCoordinator(coordinator);
    apply = widget.findChild<QDialogButtonBox *>("mButtonBox")->button(QDialogButtonBox::Apply);
    online = widget.findChild<QAbstractButton *>("onlineSessionSwitch");
    provider = static_cast<oclero::qlementine::SegmentedControl *>(
        widget.findChild<QWidget *>("providerKindControl"));
    title = widget.findChild<QLineEdit *>("mTitle");
    urlEdit = widget.findChild<QLineEdit *>("meetingUrlEdit");
    open = widget.findChild<QPushButton *>("openMeetingButton");
    copyLink = widget.findChild<QPushButton *>("copyMeetingUrlButton");
    copyInvite = widget.findChild<QPushButton *>("copyMeetingInviteButton");
    copyPasscode = widget.findChild<QPushButton *>("copyMeetingPasscodeButton");
    panel = widget.findChild<QWidget *>("seriesCallPanel");
    status = widget.findChild<QLabel *>("seriesCallStatusLabel");
    retry = widget.findChild<QPushButton *>("seriesCallRetryButton");
    reissue = widget.findChild<QPushButton *>("seriesCallReissueButton");
    publish = widget.findChild<QPushButton *>("seriesCallPublishButton");
    migrate = widget.findChild<QPushButton *>("migrateToPermanentLinkButton");
    QObject::connect(&widget, &QEventDetailsWidget::provideEventSave, &widget,
                     [this](QEventItem *event) {
                       saves.push_back({event->providerKind(), event->meetingRef(),
                                        event->meetingUrl(), event->invitationState()});
                       if (event->getId() <= 0) {
                         event->setId(101);
                       }
                     });
  }
  [[nodiscard]] bool allFound() const {
    return apply && online && provider && title && urlEdit && open && copyLink && copyInvite &&
           copyPasscode && panel && status && retry && reissue && publish && migrate;
  }

  QEventDetailsWidget widget;
  QPushButton *apply = nullptr;
  QAbstractButton *online = nullptr;
  oclero::qlementine::SegmentedControl *provider = nullptr;
  QLineEdit *title = nullptr;
  QLineEdit *urlEdit = nullptr;
  QPushButton *open = nullptr;
  QPushButton *copyLink = nullptr;
  QPushButton *copyInvite = nullptr;
  QPushButton *copyPasscode = nullptr;
  QWidget *panel = nullptr;
  QLabel *status = nullptr;
  QPushButton *retry = nullptr;
  QPushButton *reissue = nullptr;
  QPushButton *publish = nullptr;
  QPushButton *migrate = nullptr;
  std::vector<SavedEvent> saves;
};

class SeriesCallUiTest : public SeriesCallFixture {
protected:
  // Creates a published series and waits until its schedule is acknowledged and
  // its permanent invitation is stored.
  int64_t publishedSeries() {
    mSync->start();
    const auto seriesId =
        mTimeline->addEventSeries(liveKitEvent(), 0, kRule, std::nullopt, "Europe/Berlin");
    mService->ensureInvitation(seriesId);
    EXPECT_TRUE(waitFor([&] {
      return mService->status(seriesId).invitation.state == InvitationState::Ready;
    }));
    return seriesId;
  }

  std::unique_ptr<QEventItem> occurrenceItem() {
    auto item = std::make_unique<QEventItem>(-77, QStringLiteral("Private title"), at(18, 0), at(19, 0));
    item->setOnline(true);
    item->setProviderKind(ProviderKind::LiveKit);
    return item;
  }
};

} // namespace

TEST_F(SeriesCallUiTest, ApplyingANewRecurringLiveKitEventCreatesNoSingleMeeting) {
  Form form(mCoordinator.get());
  ASSERT_TRUE(form.allFound());
  form.widget.startCreatingNewEvent(QDate(2026, 10, 27), QTime(18, 0), 60);
  form.online->setChecked(true);
  form.provider->setCurrentIndex(1);
  form.widget.setRecurrenceRule(QStringLiteral("FREQ=WEEKLY;INTERVAL=1;BYDAY=TU"));
  ASSERT_TRUE(form.widget.isRecurring());

  form.apply->click();

  // Saved at once (nothing to wait for) as a LiveKit event without any meeting
  // of its own: the page turns it into ONE published series.
  ASSERT_EQ(form.saves.size(), 1u);
  EXPECT_EQ(form.saves[0].kind, ProviderKind::LiveKit);
  EXPECT_TRUE(form.saves[0].meetingRef.isEmpty());
  EXPECT_FALSE(form.saves[0].invitationState.has_value());
  QTest::qWait(100);
  EXPECT_TRUE(mModel.requestLog.isEmpty()); // no POST /v1/meetings
}

TEST_F(SeriesCallUiTest, NonRecurringLiveKitEventStillCreatesItsSingleMeeting) {
  Form form(mCoordinator.get());
  ASSERT_TRUE(form.allFound());
  form.widget.startCreatingNewEvent(QDate(2026, 10, 27), QTime(18, 0), 60);
  form.online->setChecked(true);
  form.provider->setCurrentIndex(1);
  ASSERT_FALSE(form.widget.isRecurring());

  form.apply->click();
  EXPECT_TRUE(form.saves.empty());
  ASSERT_TRUE(waitFor([&] { return !form.saves.empty(); }));

  EXPECT_EQ(form.saves[0].meetingRef, "ref-new");
  EXPECT_EQ(mBackend->requestCount("POST", "/v1/meetings"), 1);
}

TEST_F(SeriesCallUiTest, PublishedOccurrenceTitleEditKeepsTheSeriesCallAndTouchesNoBackend) {
  const auto seriesId = publishedSeries();
  ASSERT_GT(seriesId, 0);
  const auto requestsBefore = mModel.requestLog.size();
  Form form(mCoordinator.get());
  ASSERT_TRUE(form.allFound());
  auto item = occurrenceItem();
  form.widget.startEditingEvent(item.get());
  form.widget.setSeriesCall(mService.get(), seriesId, "series:target");

  EXPECT_FALSE(form.online->isEnabled()); // an occurrence cannot be detached from its series
  EXPECT_FALSE(form.provider->isEnabled());
  form.title->setText("Renamed");
  form.apply->click();

  ASSERT_EQ(form.saves.size(), 1u);
  EXPECT_EQ(form.saves[0].kind, ProviderKind::LiveKit);
  EXPECT_TRUE(form.saves[0].meetingRef.isEmpty());
  QTest::qWait(100);
  EXPECT_EQ(mModel.requestLog.size(), requestsBefore);
  EXPECT_EQ(mModel.invitations, 1); // the invitation is untouched
}

TEST_F(SeriesCallUiTest, EveryOccurrenceOpensItsOwnTargetAndCopiesTheSameInvitation) {
  const auto seriesId = publishedSeries();
  ASSERT_GT(seriesId, 0);
  Form first(mCoordinator.get());
  Form second(mCoordinator.get());
  auto item1 = occurrenceItem();
  auto item2 = occurrenceItem();
  first.widget.startEditingEvent(item1.get());
  first.widget.setSeriesCall(mService.get(), seriesId, "series:first");
  second.widget.startEditingEvent(item2.get());
  second.widget.setSeriesCall(mService.get(), seriesId, "series:second");

  EXPECT_TRUE(first.status->text().contains("Permanent link ready"));
  EXPECT_TRUE(first.copyLink->isEnabled());
  EXPECT_TRUE(first.copyPasscode->isEnabled());
  EXPECT_FALSE(first.urlEdit->text().contains("http")); // the secret is not shown or stored in the form

  QSignalSpy openFirst(&first.widget, &QEventDetailsWidget::openLiveKitMeetingRequested);
  QSignalSpy openSecond(&second.widget, &QEventDetailsWidget::openLiveKitMeetingRequested);
  first.open->click();
  second.open->click();
  ASSERT_EQ(openFirst.count(), 1);
  ASSERT_EQ(openSecond.count(), 1);
  EXPECT_EQ(openFirst.at(0).at(0).toString(), "series:first");
  EXPECT_EQ(openSecond.at(0).at(0).toString(), "series:second");

  const int readsBefore = mStore->reads;
  QApplication::clipboard()->clear();
  first.copyLink->click();
  const auto fromFirst = QApplication::clipboard()->text();
  QApplication::clipboard()->clear();
  second.copyLink->click();
  const auto fromSecond = QApplication::clipboard()->text();
  EXPECT_TRUE(fromFirst.startsWith("https://sessio.test/i/"));
  EXPECT_EQ(fromFirst, fromSecond);
  EXPECT_EQ(mStore->reads, readsBefore + 2); // read from secure storage each time
  first.copyPasscode->click();
  EXPECT_EQ(QApplication::clipboard()->text(), "pass0001");
}

TEST_F(SeriesCallUiTest, UnavailableKeychainIsReportedWhenCopyingTheLink) {
  const auto seriesId = publishedSeries();
  Form form(mCoordinator.get());
  auto item = occurrenceItem();
  form.widget.startEditingEvent(item.get());
  form.widget.setSeriesCall(mService.get(), seriesId, "series:t");
  mStore->available = false;
  QStringList texts;
  QTimer closer;
  QObject::connect(&closer, &QTimer::timeout, [&]() {
    for (auto *widget : QApplication::topLevelWidgets()) {
      if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
        texts.append(box->text());
        box->done(QMessageBox::Ok);
      }
    }
  });
  closer.start(10);

  form.copyLink->click();

  EXPECT_EQ(texts.size(), 1);
  EXPECT_TRUE(texts.value(0).contains("keychain"));
}

TEST_F(SeriesCallUiTest, OfflineCancellationIsShownInlineAsNotSyncedWithManualRetry) {
  const auto seriesId = publishedSeries();
  ASSERT_GT(seriesId, 0);
  Form form(mCoordinator.get());
  auto item = occurrenceItem();
  form.widget.startEditingEvent(item.get());
  form.widget.setSeriesCall(mService.get(), seriesId, "series:t");
  mClient->setScheduleRequestTimeoutMs(300);
  mModel.neverRespondToPut = true;

  const auto occurrence = occurrenceAt(kFirst + 7 * kDay + kHour);
  ASSERT_TRUE(occurrence.has_value());
  mTimeline->removeEvent(occurrence->id);

  ASSERT_TRUE(waitFor([&] { return form.status->text().contains("may still allow entry"); }));
  EXPECT_TRUE(form.panel->isVisibleTo(&form.widget));
  ASSERT_TRUE(waitFor([&] { return form.retry->isVisibleTo(&form.widget); }, 6000));

  mModel.neverRespondToPut = false;
  form.retry->click();
  ASSERT_TRUE(waitFor([&] { return form.status->text().contains("Schedule synchronized"); }, 6000));
  EXPECT_FALSE(form.status->text().contains("may still allow entry"));
  EXPECT_FALSE(form.retry->isVisibleTo(&form.widget));
}

TEST_F(SeriesCallUiTest, ConflictIsShownInlineWithAnExplicitPublishAction) {
  const auto seriesId = publishedSeries();
  ASSERT_GT(seriesId, 0);
  Form form(mCoordinator.get());
  auto item = occurrenceItem();
  form.widget.startEditingEvent(item.get());
  form.widget.setSeriesCall(mService.get(), seriesId, "series:t");

  const auto occurrence = occurrenceAt(kFirst + 7 * kDay + kHour);
  ASSERT_TRUE(occurrence.has_value());
  mModel.series.begin().value().revision += 5; // the server moved on: the next PUT conflicts
  mTimeline->removeEvent(occurrence->id);

  ASSERT_TRUE(waitFor([&] { return form.status->text().contains("different version"); }));
  EXPECT_TRUE(form.publish->isVisibleTo(&form.widget));
  EXPECT_FALSE(form.retry->isVisibleTo(&form.widget)); // a retry would not resolve a conflict
  EXPECT_EQ(mDb->get_schedule_identity(seriesId)->sync_state, "conflict");
}

TEST_F(SeriesCallUiTest, LegacyLiveKitSeriesOffersTheOptInMigrationAction) {
  const auto seriesId = createLegacyLiveKitSeries();
  ASSERT_GT(seriesId, 0);
  Form form(mCoordinator.get());
  auto item = occurrenceItem();
  item->setMeetingRef("ref-old");
  form.widget.startEditingEvent(item.get());
  form.widget.setLegacyMigration(mService.get(), seriesId);

  EXPECT_TRUE(form.migrate->isVisibleTo(&form.widget));
  EXPECT_TRUE(form.online->isEnabled()); // legacy behaviour is untouched until opt-in
  QSignalSpy requested(&form.widget, &QEventDetailsWidget::migrateToPermanentLinkRequested);
  form.migrate->click();
  ASSERT_EQ(requested.count(), 1);
  EXPECT_EQ(requested.at(0).at(0).toLongLong(), seriesId);

  // Legacy editing still replaces its single meeting (no series shortcut).
  EXPECT_FALSE(form.widget.isSeriesBacked());
}

int main(int argc, char **argv) {
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
