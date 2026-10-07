#include "transcript_page.h"
#include "config.h"
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QPointer>
#include <QTemporaryDir>
#include <gtest/gtest.h>

class TranscriptPageTest : public ::testing::Test {
protected:
  void SetUp() override {
    pcm::config::DatabaseConfig databaseConfig;
    databaseConfig.db_pth = Poco::Path(temp.path().toStdString());
    pcm::config::Config config{.db_conf = databaseConfig};
    db = std::make_shared<pcm::database::Database>(config);
    DuckEvent e; e.name = "Session"; e.start_date = 1730000000000;
    e.end_date = *e.start_date + 3600000; e.duration = 3600;
    e.event_stat_id = 1; e.payment_stat_id = 1;
    event = db->add_event(e);
    id = db->add_transcript(event, "live_local_v1", std::string("model"));
    DuckTranscriptPhrase p; p.transcript_id = id; p.track_role = "participant";
    p.text = "Second"; p.start_ms = 2000; p.end_ms = 2500;
    second = db->add_transcript_phrase(p);
    p.text = "First"; p.start_ms = 1000; p.end_ms = 1500;
    first = db->add_transcript_phrase(p);
    db->set_transcript_status(id, "draft");
    page = std::make_unique<TranscriptPage>(db, event, "Session");
  }
  QPushButton *button(const QString &name) { return page->findChild<QPushButton *>(name); }
  QTemporaryDir temp;
  std::shared_ptr<pcm::database::Database> db;
  std::unique_ptr<TranscriptPage> page;
  int64_t event, id, first, second;
};
TEST_F(TranscriptPageTest, ShowsNewestTranscriptWithPhrasesInOrder) {
  EXPECT_EQ(page->transcriptCount(), 1);
  auto labels = page->findChildren<QLabel *>("phraseText");
  ASSERT_EQ(labels.size(), 2); EXPECT_EQ(labels[0]->text(), "First");
  EXPECT_EQ(labels[1]->text(), "Second");
}
TEST_F(TranscriptPageTest, ComboListsEveryTranscriptNewestFirst) {
  auto newer = db->add_transcript(event, "live_local_v1", std::string("new model"));
  db->set_transcript_status(newer, "draft"); page->reload(event, "Session");
  auto combo = page->findChild<QComboBox *>("transcriptSelector");
  ASSERT_EQ(combo->count(), 2); EXPECT_EQ(combo->currentData().toLongLong(), newer);
  EXPECT_EQ(combo->itemData(1).toLongLong(), id);
  EXPECT_TRUE(page->findChildren<QLabel *>("phraseText").empty());
  EXPECT_TRUE(page->findChild<QLabel *>("transcriptMetadata")->text().contains("new model"));
  combo->setCurrentIndex(1);
  EXPECT_EQ(page->findChildren<QLabel *>("phraseText").size(), 2);
}
TEST_F(TranscriptPageTest, MarkReviewedUpdatesStatusAndChip) {
  QSignalSpy spy(page.get(), &TranscriptPage::transcriptsChanged);
  button("markReviewed")->click(); EXPECT_EQ(db->get_transcript(id)->status, "reviewed");
  EXPECT_EQ(page->findChild<QLabel *>("transcriptStatus")->text(), "Reviewed");
  EXPECT_TRUE(button("markReviewed")->isHidden());
  EXPECT_EQ(spy.count(), 1);
}
TEST_F(TranscriptPageTest, RecordingTranscriptDisablesEditDeleteAndReview) {
  db->set_transcript_status(id, "recording"); page->reload(event, "Session");
  EXPECT_FALSE(button("markReviewed")->isEnabled());
  EXPECT_FALSE(button("deleteTranscript")->isEnabled());
  EXPECT_FALSE(button("editPhrase_" + QString::number(first))->isEnabled());
  EXPECT_FALSE(button("deletePhrase_" + QString::number(first))->isEnabled());
}
TEST_F(TranscriptPageTest, EditingAPhraseSavesTextAndMarksEdited) {
  button("editPhrase_" + QString::number(first))->click();
  page->findChild<QPlainTextEdit *>("phraseEditor")->setPlainText("Changed");
  button("savePhrase")->click(); auto phrases = db->get_transcript_phrases(id);
  EXPECT_EQ(phrases[0].text, "Changed"); EXPECT_TRUE(phrases[0].edited);
  EXPECT_EQ(page->findChildren<QLabel *>("phraseEdited").size(), 1);
}
TEST_F(TranscriptPageTest, EmptyEditIsRefused) {
  button("editPhrase_" + QString::number(first))->click();
  page->findChild<QPlainTextEdit *>("phraseEditor")->setPlainText("  ");
  button("savePhrase")->click(); EXPECT_EQ(db->get_transcript_phrases(id)[0].text, "First");
}
TEST_F(TranscriptPageTest, UnsavedEditBlocksOtherPhraseAndReview) {
  button("editPhrase_" + QString::number(first))->click();
  auto *editor = page->findChild<QPlainTextEdit *>("phraseEditor");
  ASSERT_NE(editor, nullptr);
  editor->setPlainText("Unsaved work");
  button("editPhrase_" + QString::number(second))->click();
  EXPECT_EQ(page->findChildren<QPlainTextEdit *>("phraseEditor").size(), 1);
  button("markReviewed")->click();
  ASSERT_EQ(page->findChild<QPlainTextEdit *>("phraseEditor"), editor);
  EXPECT_EQ(editor->toPlainText(), "Unsaved work");
  EXPECT_EQ(db->get_transcript(id)->status, "draft");
  button("savePhrase")->click();
  EXPECT_EQ(db->get_transcript_phrases(id).front().text, "Unsaved work");
}
TEST_F(TranscriptPageTest, DeletingAPhraseRemovesItFromDbAndList) {
  button("deletePhrase_" + QString::number(first))->click();
  EXPECT_EQ(db->get_transcript_phrases(id).size(), 1);
  EXPECT_EQ(page->findChildren<QLabel *>("phraseText").size(), 1);
}
TEST_F(TranscriptPageTest, DeleteTranscriptAsksConfirmationAndRemovesEverything) {
  int confirmations = 0; bool accept = false;
  page->setConfirmHook([&](const QString &) { ++confirmations; return accept; });
  QSignalSpy spy(page.get(), &TranscriptPage::transcriptsChanged);
  button("deleteTranscript")->click(); EXPECT_EQ(page->transcriptCount(), 1);
  accept = true; button("deleteTranscript")->click();
  EXPECT_EQ(confirmations, 2); EXPECT_EQ(page->transcriptCount(), 0);
  EXPECT_TRUE(db->get_transcript_phrases(id).empty()); EXPECT_EQ(spy.count(), 1);
  EXPECT_EQ(page->findChild<QLabel *>("transcriptNotice")->text(), "No transcripts for this event.");
}
TEST_F(TranscriptPageTest, MutationsRecheckRecordingAfterPageOpened) {
  button("editPhrase_" + QString::number(first))->click();
  page->findChild<QPlainTextEdit *>("phraseEditor")->setPlainText("Changed");
  db->set_transcript_status(id, "recording"); button("savePhrase")->click();
  button("deletePhrase_" + QString::number(first))->click();
  button("markReviewed")->click();
  page->setConfirmHook([](const QString &) { return true; });
  button("deleteTranscript")->click();
  EXPECT_EQ(db->get_transcript(id)->status, "recording");
  EXPECT_EQ(db->get_transcript_phrases(id).size(), 2);
  EXPECT_EQ(db->get_transcript_phrases(id)[0].text, "First");
}
TEST_F(TranscriptPageTest, DeleteRechecksRecordingAfterConfirmation) {
  page->setConfirmHook([&](const QString &) { db->set_transcript_status(id, "recording"); return true; });
  button("deleteTranscript")->click(); EXPECT_NE(db->get_transcript(id), nullptr);
}
TEST_F(TranscriptPageTest, BackRequestsEventAndReloadSupportsAnotherEvent) {
  QSignalSpy spy(page.get(), &TranscriptPage::backRequested);
  button("backToEvent")->click(); EXPECT_EQ(spy.count(), 1);
  page->reload(-1, "Missing"); EXPECT_EQ(page->transcriptCount(), 0);
}
TEST_F(TranscriptPageTest, HeaderShowsLinkedClientAndSafeMissingClientFallback) {
  EXPECT_EQ(page->findChild<QLabel *>("transcriptClient")->text(), "Client unavailable");
  DuckClient client;
  client.name = "Test"; client.last_name = "Client";
  const auto clientId = db->add_client(client);
  db->add_event_client(event, clientId);
  page->reload(event, "Session");
  EXPECT_EQ(page->findChild<QLabel *>("transcriptClient")->text(), "Client: Test Client");
}
TEST_F(TranscriptPageTest, ConfirmationMayDestroyPageWithoutMutation) {
  auto *raw = page.release();
  QPointer<TranscriptPage> guard(raw);
  raw->setConfirmHook([raw](const QString &) { delete raw; return true; });
  raw->findChild<QPushButton *>("deleteTranscript")->click();
  EXPECT_TRUE(guard.isNull());
  EXPECT_NE(db->get_transcript(id), nullptr);
}
TEST_F(TranscriptPageTest, ConfirmationMayNavigateWithoutDeletingPreviousEvent) {
  page->setConfirmHook([&](const QString &) { page->reload(-1, "Missing"); return true; });
  button("deleteTranscript")->click();
  EXPECT_NE(db->get_transcript(id), nullptr);
}
TEST_F(TranscriptPageTest, PhraseTimesIncludeHours) {
  DuckTranscriptPhrase phrase;
  phrase.transcript_id = id; phrase.track_role = "participant";
  phrase.text = "Late phrase"; phrase.start_ms = 3601000; phrase.end_ms = 3602000;
  db->set_transcript_status(id, "recording");
  ASSERT_GT(db->add_transcript_phrase(phrase), 0);
  db->set_transcript_status(id, "draft");
  page->reload(event, "Session");
  const auto labels = page->findChildren<QLabel *>("phraseSpeaker");
  ASSERT_EQ(labels.size(), 3);
  EXPECT_TRUE(labels.last()->text().startsWith("1:00:01 – 1:00:02"));
}
TEST_F(TranscriptPageTest, CancelEditKeepsOriginalPhrase) {
  button("editPhrase_" + QString::number(first))->click();
  page->findChild<QPlainTextEdit *>("phraseEditor")->setPlainText("Discarded");
  button("cancelPhrase")->click();
  EXPECT_EQ(db->get_transcript_phrases(id)[0].text, "First");
  EXPECT_EQ(page->findChild<QPlainTextEdit *>("phraseEditor"), nullptr);
}
int main(int argc, char **argv) {
  QTemporaryDir home; qputenv("HOME", home.path().toUtf8());
  qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
  QApplication app(argc, argv); ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS();
}
