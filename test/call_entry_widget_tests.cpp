#include "call_entry_widget.h"
#include "app_settings.h"

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QSettings>
#include <QTemporaryDir>
#include <QQuickWidget>
#include <QQuickItem>
#include <QPalette>
#include <gtest/gtest.h>

TEST(CallEntryWidgetTest, QuickWorkspacePreservesJoinAndHistoryContracts) {
  CallEntryWidget widget(true);
  widget.enableQuickWorkspace();
  widget.resize(1400, 900); widget.show();
  auto *quick = widget.findChild<QQuickWidget *>("meetingsQuickWorkspace");
  ASSERT_NE(quick, nullptr);
  ASSERT_EQ(quick->status(), QQuickWidget::Ready);
  auto *header = widget.headerControls();
  EXPECT_EQ(header, widget.headerControls());
  EXPECT_TRUE(quick->rootObject()->property("externalHeader").toBool());
  widget.setUpcomingMeetings({{"ref-1", "Synthetic meeting", QDateTime::currentDateTime(), true, 7},
    {"ref-2", "Unavailable", QDateTime::currentDateTime(), false, 8}});
  QSignalSpy own(&widget, &CallEntryWidget::ownMeetingJoinRequested);
  widget.joinOwn("ref-2"); widget.joinOwn("missing");
  EXPECT_EQ(own.count(), 0);
  widget.joinOwn("ref-1");
  EXPECT_EQ(own.count(), 1);
  widget.preselectOwnMeeting("ref-2");
  EXPECT_EQ(quick->rootObject()->property("selectedId").toString(), "ref-2");
  widget.setDisplayName("Synthetic name");
  QSignalSpy invitation(&widget, &CallEntryWidget::joinByCodeRequested);
  widget.joinInvitation("demo-code", "123456");
  auto *dialog = quick->rootObject()->findChild<QObject *>("qmlMeetingInvitation");
  ASSERT_NE(dialog, nullptr);
  EXPECT_TRUE(dialog->property("visible").toBool());
  dialog->setProperty("visible", false);
  EXPECT_EQ(widget.displayName(), "Synthetic name");
  EXPECT_EQ(invitation.count(), 1);
  EXPECT_EQ(widget.code(), "demo-code");
  widget.setMaterialHistory({QVariantMap{{"id", "17"}, {"title", "Saved meeting"}, {"date", "26.10.2026"},
    {"transcripts", QVariantList{QVariantMap{{"id", "23"}, {"title", "Recording"}}}}}});
  QSignalSpy material(&widget, &CallEntryWidget::materialRequested), transcript(&widget, &CallEntryWidget::transcriptRequested);
  widget.openMaterial("17"); widget.openMaterial("unknown"); widget.openTranscript("23"); widget.openTranscript("unknown");
  EXPECT_EQ(material.count(), 1); EXPECT_EQ(transcript.count(), 1);
  if (qEnvironmentVariableIsSet("SESSIO_MEETINGS_SCREENSHOT")) {
    if (qEnvironmentVariableIsSet("SESSIO_PREVIEW_POPUP")) {
      auto *mode = quick->rootObject()->findChild<QQuickItem *>("qmlMeetingMode");
      if (mode) {
        auto *popup = mode->property("popup").value<QObject *>();
        if (popup) QMetaObject::invokeMethod(popup, "open");
      }
    }
    QTest::qWait(100); widget.grab().save(qEnvironmentVariable("SESSIO_MEETINGS_SCREENSHOT"));
  }
}

TEST(CallEntryWidgetTest, SpecialistModeShowsOwnMeetingsListAndEmitsJoinRequest) {
  CallEntryWidget widget(/*showOwnMeetings=*/true);
  widget.setUpcomingMeetings({{"ref-1", "14:00 — Иванова", QDateTime::currentDateTime(), true, 7}});

  auto *joinButton = widget.findChild<QPushButton *>("joinOwnMeetingButton_ref-1");
  ASSERT_NE(joinButton, nullptr);

  QSignalSpy joinSpy(&widget, &CallEntryWidget::ownMeetingJoinRequested);
  QTest::mouseClick(joinButton, Qt::LeftButton);

  ASSERT_EQ(joinSpy.count(), 1);
  EXPECT_EQ(joinSpy.at(0).at(0).toString(), QStringLiteral("ref-1"));
}

// Layout contract: the name card sits on top, the join group on the left and the practitioner's
// calls for today on the right; a client sees only the join group.
TEST(CallEntryWidgetTest, SpecialistModeSplitsJoinAndTodaysCallsIntoTwoGroupsUnderTheNameCard) {
  CallEntryWidget widget(/*showOwnMeetings=*/true);
  widget.resize(900, 600);
  widget.show();
  auto *nameCard = widget.findChild<QWidget *>("nameCard");
  auto *joinGroup = widget.findChild<QWidget *>("joinGroup");
  auto *callsGroup = widget.findChild<QWidget *>("callsGroup");
  ASSERT_NE(nameCard, nullptr);
  ASSERT_NE(joinGroup, nullptr);
  ASSERT_NE(callsGroup, nullptr);
  const auto pos = [&](QWidget *w) { return w->mapTo(&widget, QPoint(0, 0)); };
  EXPECT_LT(pos(nameCard).y(), pos(joinGroup).y());
  EXPECT_LT(pos(nameCard).y(), pos(callsGroup).y());
  EXPECT_LT(pos(joinGroup).x(), pos(callsGroup).x());
}

TEST(CallEntryWidgetTest, EmptyCallsGroupSaysThereAreNoCallsToday) {
  CallEntryWidget widget(/*showOwnMeetings=*/true);
  widget.show();
  auto *empty = widget.findChild<QWidget *>("noCallsLabel");
  ASSERT_NE(empty, nullptr);
  EXPECT_TRUE(empty->isVisibleTo(&widget));
  widget.setUpcomingMeetings({{"ref-1", "Anna", QDateTime::currentDateTime(), true, 7}});
  EXPECT_FALSE(empty->isVisibleTo(&widget));
  widget.setUpcomingMeetings({});
  EXPECT_TRUE(empty->isVisibleTo(&widget));
}

TEST(CallEntryWidgetTest, ClientModeHasNoCallsGroup) {
  CallEntryWidget widget(/*showOwnMeetings=*/false);
  EXPECT_EQ(widget.findChild<QWidget *>("callsGroup"), nullptr);
  EXPECT_NE(widget.findChild<QWidget *>("joinGroup"), nullptr);
}

TEST(CallEntryWidgetTest, ClientModeHasNoOwnMeetingsList) {
  CallEntryWidget widget(/*showOwnMeetings=*/false);
  auto *list = widget.findChild<QWidget *>("ownMeetingsList");
  EXPECT_EQ(list, nullptr);
}

TEST(CallEntryWidgetTest, PerCallNameOverridesSavedDefaultWithoutChangingIt) {
  pcm::app_settings::setCallDisplayName(" Анна ");
  for (bool specialist : {false, true}) {
    CallEntryWidget widget(specialist);
    widget.show();
    EXPECT_EQ(widget.displayName(), "Анна");
    widget.findChild<QLineEdit *>("callDisplayNameEdit")->setText(" Анна на встрече ");
    EXPECT_EQ(widget.displayName(), "Анна на встрече");
    EXPECT_EQ(pcm::app_settings::callDisplayName(), "Анна");
    widget.hide();
    widget.show();
    EXPECT_EQ(widget.displayName(), "Анна");
  }
}

TEST(CallEntryWidgetTest, SubmittingCodeFormEmitsJoinByCodeRequested) {
  CallEntryWidget widget(/*showOwnMeetings=*/false);
  auto *codeEdit = widget.findChild<QLineEdit *>("joinCodeEdit");
  auto *passcodeEdit = widget.findChild<QLineEdit *>("joinPasscodeEdit");
  auto *connectButton = widget.findChild<QPushButton *>("joinByCodeButton");
  ASSERT_NE(codeEdit, nullptr);
  ASSERT_NE(passcodeEdit, nullptr);
  ASSERT_NE(connectButton, nullptr);

  codeEdit->setText("code-1");
  passcodeEdit->setText("123456");
  QSignalSpy joinSpy(&widget, &CallEntryWidget::joinByCodeRequested);
  QTest::mouseClick(connectButton, Qt::LeftButton);

  ASSERT_EQ(joinSpy.count(), 1);
  EXPECT_EQ(joinSpy.at(0).at(0).toString(), QStringLiteral("code-1"));
  EXPECT_EQ(joinSpy.at(0).at(1).toString(), QStringLiteral("123456"));
}

TEST(CallEntryWidgetTest, PrefillJoinCodePopulatesForm) {
  CallEntryWidget widget(false);
  widget.prefillJoinCode("code-2", "654321");

  EXPECT_EQ(widget.findChild<QLineEdit *>("joinCodeEdit")->text(), QStringLiteral("code-2"));
  EXPECT_EQ(widget.findChild<QLineEdit *>("joinPasscodeEdit")->text(), QStringLiteral("654321"));
}

TEST(CallEntryWidgetTest, ShowErrorDisplaysMessageAndClearErrorHidesIt) {
  CallEntryWidget widget(/*showOwnMeetings=*/false);
  auto *errorLabel = widget.findChild<QLabel *>("joinErrorLabel");
  ASSERT_NE(errorLabel, nullptr);
  EXPECT_TRUE(errorLabel->isHidden());

  widget.showError("invalid_passcode");
  EXPECT_FALSE(errorLabel->isHidden());
  EXPECT_EQ(errorLabel->text(), QStringLiteral("invalid_passcode"));

  widget.clearError();
  EXPECT_TRUE(errorLabel->isHidden());
  EXPECT_TRUE(errorLabel->text().isEmpty());
}

TEST(CallEntryWidgetTest, ShowingTheEntryScreenAsksForAFreshMeetingList) {
  CallEntryWidget widget(true);
  QSignalSpy spy(&widget, &CallEntryWidget::shown);
  widget.show();
  EXPECT_EQ(spy.count(), 1);
  widget.hide();
  widget.show();
  EXPECT_EQ(spy.count(), 2);
}

TEST(AppSettingsTest, TranscriptionDefaultsEnabledAndPersistsRoundTrip) {
  QSettings().remove("transcription/enabled");
  EXPECT_TRUE(pcm::app_settings::transcriptionEnabled());
  pcm::app_settings::setTranscriptionEnabled(false);
  QSettings().sync();
  EXPECT_FALSE(QSettings().value("transcription/enabled", true).toBool());
  EXPECT_FALSE(pcm::app_settings::transcriptionEnabled());
  pcm::app_settings::setTranscriptionEnabled(true);
  QSettings().sync();
  EXPECT_TRUE(QSettings().value("transcription/enabled", false).toBool());
  EXPECT_TRUE(pcm::app_settings::transcriptionEnabled());
}

TEST(AppSettingsTest, TranscriptionTuningDefaultsPersistAndAreClamped) {
  using pcm::transcription::TranscriptionTuning;
  for (const char *key : {"vadThreshold", "vadMinSilence", "vadMinSpeech", "vadMaxPhrase", "threads"})
    QSettings().remove(QStringLiteral("transcription/") + key);
  EXPECT_EQ(pcm::app_settings::transcriptionTuning(), TranscriptionTuning{});

  TranscriptionTuning chosen;
  chosen.threshold = 0.65F; chosen.minSilenceSec = 1.2F; chosen.minSpeechSec = 0.4F;
  chosen.maxPhraseSec = 12.0F; chosen.numThreads = 2;
  pcm::app_settings::setTranscriptionTuning(chosen);
  QSettings().sync();
  EXPECT_EQ(pcm::app_settings::transcriptionTuning(), chosen);

  // A hand-edited or corrupt config can never reach the engine out of range.
  QSettings().setValue("transcription/vadThreshold", 7.0);
  QSettings().setValue("transcription/threads", 0);
  QSettings().setValue("transcription/vadMaxPhrase", "garbage");
  const auto clamped = pcm::app_settings::transcriptionTuning();
  EXPECT_FLOAT_EQ(clamped.threshold, TranscriptionTuning::kMaxThreshold);
  EXPECT_EQ(clamped.numThreads, TranscriptionTuning::kMinThreads);
  EXPECT_FLOAT_EQ(clamped.maxPhraseSec, TranscriptionTuning::kMinPhrase);
  for (const char *key : {"vadThreshold", "vadMinSilence", "vadMinSpeech", "vadMaxPhrase", "threads"})
    QSettings().remove(QStringLiteral("transcription/") + key);
}

int main(int argc, char **argv) {
  QTemporaryDir settingsDir;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
  QApplication app(argc, argv);
  if (qEnvironmentVariableIsSet("SESSIO_PREVIEW_THEME")) {
    auto colors = app.palette();
    colors.setColor(QPalette::Window, QColor("#282b33"));
    colors.setColor(QPalette::Base, QColor("#1f2127"));
    colors.setColor(QPalette::Text, QColor("#f0eff5"));
    colors.setColor(QPalette::WindowText, QColor("#f0eff5"));
    colors.setColor(QPalette::ButtonText, QColor("#f0eff5"));
    colors.setColor(QPalette::Mid, QColor("#3c414f"));
    colors.setColor(QPalette::Highlight, QColor("#b49af5"));
    colors.setColor(QPalette::HighlightedText, QColor("#231a35"));
    app.setPalette(colors);
  }
  app.setOrganizationName("SessioTests");
  app.setApplicationName("CallEntryWidget");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
