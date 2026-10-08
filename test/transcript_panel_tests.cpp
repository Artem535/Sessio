#include "transcript_panel.h"
#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <gtest/gtest.h>

using pcm::calltranscription::SessionState;
TEST(TranscriptPanelTest, ButtonsFollowState) {
  TranscriptPanel panel;
  panel.setStartAvailable(true, {});
  for (auto state : {SessionState::Idle, SessionState::Loading, SessionState::Recording,
                     SessionState::Stopping, SessionState::Finished, SessionState::Failed}) {
    panel.setState(state);
    bool start = state == SessionState::Idle || state == SessionState::Finished || state == SessionState::Failed;
    bool stop = state == SessionState::Loading || state == SessionState::Recording;
    EXPECT_EQ(!panel.findChild<QPushButton *>("transcriptStart")->isHidden(), start);
    EXPECT_EQ(!panel.findChild<QPushButton *>("transcriptStop")->isHidden(), stop);
    EXPECT_EQ(!panel.findChild<QPushButton *>("transcriptRevoke")->isHidden(), stop || state == SessionState::Stopping);
    EXPECT_TRUE(panel.findChild<QPushButton *>("transcriptStart")->isEnabled());
  }
}
TEST(TranscriptPanelTest, StartDisabledWithReasonShown) {
  TranscriptPanel panel;
  panel.setStartAvailable(false, "Speech models missing");
  EXPECT_FALSE(panel.findChild<QPushButton *>("transcriptStart")->isEnabled());
  auto *reason = panel.findChild<QLabel *>("transcriptStartReason");
  EXPECT_FALSE(reason->isHidden());
  EXPECT_EQ(reason->text(), "Speech models missing");
}
TEST(TranscriptPanelTest, AddPhraseShowsTimeSpeakerAndText) {
  TranscriptPanel panel;
  DuckTranscriptPhrase phrase;
  phrase.start_ms = 65000; phrase.speaker_name = "Alex"; phrase.text = "A sample sentence.";
  panel.addPhrase(phrase);
  phrase.start_ms = 3725000; panel.addPhrase(phrase);
  auto times = panel.findChildren<QLabel *>("phraseTime");
  ASSERT_EQ(times.size(), 2);
  EXPECT_EQ(times[0]->text(), "01:05"); EXPECT_EQ(times[1]->text(), "1:02:05");
  EXPECT_EQ(panel.findChild<QLabel *>("phraseSpeaker")->text(), "Alex");
  EXPECT_EQ(panel.findChild<QLabel *>("phraseText")->text(), "A sample sentence.");
}
TEST(TranscriptPanelTest, SpeakerFallbackByRole) {
  TranscriptPanel panel; DuckTranscriptPhrase phrase;
  phrase.track_role = "practitioner"; panel.addPhrase(phrase);
  phrase.track_role = "participant"; phrase.speaker_name = ""; panel.addPhrase(phrase);
  auto speakers = panel.findChildren<QLabel *>("phraseSpeaker");
  EXPECT_EQ(speakers[0]->text(), "You"); EXPECT_EQ(speakers[1]->text(), "Participant");
  EXPECT_TRUE(speakers[0]->font().bold());
}
TEST(TranscriptPanelTest, PhraseCountAndClear) {
  TranscriptPanel panel; panel.addPhrase({}); panel.addPhrase({});
  EXPECT_EQ(panel.phraseCount(), 2); panel.clearPhrases();
  EXPECT_EQ(panel.phraseCount(), 0); EXPECT_TRUE(panel.findChildren<QLabel *>("phraseText").empty());
}
TEST(TranscriptPanelTest, ListeningRowOnlyWhileRecording) {
  TranscriptPanel panel;
  for (auto state : {SessionState::Idle, SessionState::Loading, SessionState::Recording,
                     SessionState::Stopping, SessionState::Finished, SessionState::Failed}) {
    panel.setState(state);
    EXPECT_EQ(!panel.findChild<QLabel *>("transcriptListening")->isHidden(), state == SessionState::Recording);
  }
}
TEST(TranscriptPanelTest, DelayedHintToggles) {
  TranscriptPanel panel; panel.setDelayed(true);
  EXPECT_FALSE(panel.findChild<QLabel *>("transcriptDelayed")->isHidden());
  panel.setDelayed(false); EXPECT_TRUE(panel.findChild<QLabel *>("transcriptDelayed")->isHidden());
}
TEST(TranscriptPanelTest, ErrorBannerAndNoticeShowAndHide) {
  TranscriptPanel panel; panel.setError("Stopped unexpectedly"); panel.setNotice("One track ended");
  EXPECT_EQ(panel.findChild<QLabel *>("transcriptError")->text(), "Stopped unexpectedly");
  EXPECT_FALSE(panel.findChild<QLabel *>("transcriptNotice")->isHidden());
  panel.setError({}); panel.setNotice({});
  EXPECT_TRUE(panel.findChild<QLabel *>("transcriptError")->isHidden());
  EXPECT_TRUE(panel.findChild<QLabel *>("transcriptNotice")->isHidden());
}
TEST(TranscriptPanelTest, ButtonsEmitOnlySignals) {
  TranscriptPanel panel; panel.setStartAvailable(true, {});
  QSignalSpy start(&panel, &TranscriptPanel::startRequested), stop(&panel, &TranscriptPanel::stopRequested), revoke(&panel, &TranscriptPanel::revokeRequested);
  panel.findChild<QPushButton *>("transcriptStart")->click(); EXPECT_EQ(start.count(), 1);
  EXPECT_EQ(panel.findChild<QLabel *>("transcriptStatus")->text(), "Transcription is off");
  panel.setState(SessionState::Recording);
  panel.findChild<QPushButton *>("transcriptStop")->click(); panel.findChild<QPushButton *>("transcriptRevoke")->click();
  EXPECT_EQ(stop.count(), 1); EXPECT_EQ(revoke.count(), 1);
  EXPECT_EQ(panel.findChild<QLabel *>("transcriptStatus")->text(), "Transcription running");
}
TEST(TranscriptPanelTest, AudioGapDoesNotReplaceModelErrorAndClearsOnStop) {
  TranscriptPanel panel;
  panel.setState(SessionState::Recording);
  panel.setError("Model failed");
  panel.setAudioGap(true);
  auto *gap = panel.findChild<QLabel *>("transcriptAudioGap");
  ASSERT_NE(gap, nullptr);
  EXPECT_FALSE(gap->isHidden());
  panel.setAudioGap(false);
  EXPECT_TRUE(gap->isHidden());
  EXPECT_EQ(panel.findChild<QLabel *>("transcriptError")->text(), "Model failed");
  EXPECT_FALSE(panel.findChild<QLabel *>("transcriptError")->isHidden());
  panel.setAudioGap(true);
  panel.setState(SessionState::Stopping);
  EXPECT_TRUE(gap->isHidden());
}
TEST(TranscriptPanelTest, KeepsBottomWhenAtBottomButNotWhenScrolledUp) {
  TranscriptPanel panel; panel.resize(420, 480); panel.show(); QApplication::processEvents();
  DuckTranscriptPhrase phrase; phrase.text = "A sufficiently long phrase to form a transcript row.";
  auto *bar = panel.findChild<QScrollArea *>()->verticalScrollBar();
  for (int i = 0; i < 200; ++i) { panel.addPhrase(phrase); QApplication::processEvents(); }
  ASSERT_GT(bar->maximum(), 0); EXPECT_EQ(bar->value(), bar->maximum());
  bar->setValue(bar->maximum() / 2); int old = bar->value();
  panel.addPhrase(phrase); QApplication::processEvents(); EXPECT_EQ(bar->value(), old);
  if (qEnvironmentVariableIsSet("SESSIO_PANEL_SCREENSHOT")) {
    panel.clearPhrases(); panel.setState(SessionState::Recording);
    phrase.start_ms = 65000; phrase.track_role = "practitioner";
    phrase.text = "What would you like us to focus on today?"; panel.addPhrase(phrase);
    phrase.start_ms = 72000; phrase.track_role = "participant"; phrase.speaker_name = "Alex";
    phrase.text = "I would like to talk about balancing work and rest."; panel.addPhrase(phrase);
    phrase.start_ms = 85000; phrase.track_role = "practitioner"; phrase.speaker_name.reset();
    phrase.text = "Let's look at what a typical week feels like."; panel.addPhrase(phrase);
    QApplication::processEvents(); panel.grab().save(qEnvironmentVariable("SESSIO_PANEL_SCREENSHOT"));
  }
}
int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen"); QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS();
}
