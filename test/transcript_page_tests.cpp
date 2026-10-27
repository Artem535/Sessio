#include "transcript_page.h"
#include "config.h"
#include "call_notes_controller.h"
#include "call_notes_page.h"
#include "client_notes_page.h"
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSplitter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QPointer>
#include <QTemporaryDir>
#include <QTest>
#include <QQuickWidget>
#include <QQuickItem>
#include <oclero/qlementine/style/QlementineStyle.hpp>
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
TEST_F(TranscriptPageTest, NotesPaneCanBeHiddenAndReopenedWithoutLosingItsEditor) {
  auto *notes = new QPlainTextEdit;
  notes->setPlainText("Unsent clinical note");
  page->setNotesView(notes);
  page->show();
  EXPECT_TRUE(page->notesVisible());
  EXPECT_TRUE(page->findChild<QLabel *>("transcriptTitle")->isHidden());
  EXPECT_LT(button("markReviewed")->width(), 220);
  button("transcriptCallNotes")->click();
  EXPECT_FALSE(page->notesVisible());
  EXPECT_FALSE(page->findChild<QLabel *>("transcriptTitle")->isHidden());
  button("transcriptCallNotes")->click();
  EXPECT_TRUE(page->notesVisible());
  EXPECT_EQ(notes->toPlainText(), "Unsent clinical note");
  EXPECT_EQ(page->findChild<QSplitter *>("transcriptNotesSplitter")->count(), 2);
}

TEST_F(TranscriptPageTest, QuickReaderLoadsPersistedPhrasesAndRetainsMutationActions) {
  page->enableQuickReader();
  page->resize(1400, 900); page->show();
  auto *quick = page->findChild<QQuickWidget *>("transcriptQuickReader");
  ASSERT_NE(quick, nullptr);
  ASSERT_EQ(quick->status(), QQuickWidget::Ready);
  auto *header = page->headerControls();
  EXPECT_EQ(header, page->headerControls());
  EXPECT_TRUE(quick->rootObject()->property("externalHeader").toBool());
  auto *history = header->findChild<QPushButton *>("readerHeaderHistory");
  ASSERT_NE(history, nullptr);
  const bool wasVisible = quick->rootObject()->property("historyVisible").toBool();
  history->click();
  EXPECT_EQ(quick->rootObject()->property("historyVisible").toBool(), !wasVisible);
  ASSERT_EQ(page->readerPhrases().size(), 2);
  EXPECT_EQ(page->readerPhrases().first().toMap().value("text").toString(), "First");
  auto *search = quick->rootObject()->findChild<QQuickItem *>("qmlTranscriptSearch");
  ASSERT_NE(search, nullptr);
  search->setProperty("text", "First");
  QApplication::processEvents();
  auto *phrases = quick->rootObject()->findChild<QQuickItem *>("qmlTranscriptPhrases");
  ASSERT_NE(phrases, nullptr);
  EXPECT_EQ(phrases->property("count").toInt(), 1);
  EXPECT_TRUE(page->focusPhrase(second));
  QApplication::processEvents();
  EXPECT_TRUE(search->property("text").toString().isEmpty());
  page->readerAction("review");
  EXPECT_EQ(db->get_transcript(id)->status, "reviewed");
  if (qEnvironmentVariableIsSet("SESSIO_READER_SCREENSHOT")) {
    QTest::qWait(100);
    page->grab().save(qEnvironmentVariable("SESSIO_READER_SCREENSHOT"));
  }
}

TEST_F(TranscriptPageTest, WrappedPhraseFitsAfterResizingTheSplitView) {
  db->update_transcript_phrase_text(first, std::string(30, ' ') +
      "A long phrase that must remain fully visible when the notes pane changes width. "
      "Additional observations wrap onto several lines without losing the end of the phrase.");
  page->reload(event, "Session");
  auto *notes = new QWidget; notes->setMinimumWidth(320); page->setNotesView(notes);
  button("transcriptSourceToggle")->click();
  page->resize(880, 700); page->show(); QTest::qWait(20);
  auto *card = page->findChild<QWidget *>("phraseCard_" + QString::number(first));
  auto *text = card->findChild<QLabel *>("phraseText");
  EXPECT_GE(text->height(), text->heightForWidth(text->width()));
}

TEST_F(TranscriptPageTest, NotesLeadTheWorkspaceAndSourceNavigationRevealsTranscript) {
  auto *notes = new QPlainTextEdit;
  page->setNotesView(notes);
  page->show();
  auto *split = page->findChild<QSplitter *>("transcriptNotesSplitter");
  ASSERT_NE(split, nullptr);
  EXPECT_TRUE(split->widget(0)->isAncestorOf(notes));
  EXPECT_TRUE(page->findChild<QWidget *>("transcriptSourcePanel")->isHidden());
  EXPECT_TRUE(page->focusPhrase(first));
  EXPECT_FALSE(page->findChild<QWidget *>("transcriptSourcePanel")->isHidden());
  EXPECT_TRUE(page->notesVisible());
  button("transcriptSourceToggle")->click();
  EXPECT_TRUE(page->findChild<QWidget *>("transcriptSourcePanel")->isHidden());
}

TEST_F(TranscriptPageTest, PhraseSearchFiltersTextAndClearingRestoresAllPhrases) {
  auto *search = page->findChild<QLineEdit *>("transcriptSearch");
  ASSERT_NE(search, nullptr);
  search->setText("second");
  EXPECT_TRUE(page->findChild<QWidget *>("phraseCard_" + QString::number(first))->isHidden());
  EXPECT_FALSE(page->findChild<QWidget *>("phraseCard_" + QString::number(second))->isHidden());
  search->clear();
  EXPECT_FALSE(page->findChild<QWidget *>("phraseCard_" + QString::number(first))->isHidden());
}

TEST_F(TranscriptPageTest, SourceNavigationOpensAnotherTranscriptAndClearsSearch) {
  const auto other = db->add_transcript(std::nullopt, "live_local_v1");
  DuckTranscriptPhrase phrase; phrase.transcript_id = other;
  phrase.track_role = "participant"; phrase.text = "Source from an earlier meeting";
  const auto phraseId = db->add_transcript_phrase(phrase);
  db->set_transcript_status(other, "draft");
  button("editPhrase_" + QString::number(first))->click();
  page->findChild<QPlainTextEdit *>("phraseEditor")->setPlainText("Unsent phrase edit");
  EXPECT_FALSE(page->focusPhrase(phraseId));
  EXPECT_EQ(page->findChild<QPlainTextEdit *>("phraseEditor")->toPlainText(), "Unsent phrase edit");
  button("cancelPhrase")->click();
  page->findChild<QLineEdit *>("transcriptSearch")->setText("no match");
  ASSERT_TRUE(page->focusPhrase(phraseId));
  EXPECT_EQ(page->findChild<QComboBox *>("transcriptSelector")->currentData().toLongLong(), other);
  EXPECT_TRUE(page->findChild<QLineEdit *>("transcriptSearch")->text().isEmpty());
  EXPECT_FALSE(page->focusPhrase(-1));
}

TEST_F(TranscriptPageTest, TranscriptActivationIsNotRepeatedForPhraseEditsAndClearsOnDeletion) {
  QSignalSpy activated(page.get(), &TranscriptPage::transcriptActivated);
  page->reloadTranscript(id, "Session");
  EXPECT_EQ(activated.count(), 0);
  db->update_transcript_phrase_text(first, "Updated");
  page->reloadTranscript(id, "Session");
  EXPECT_EQ(activated.count(), 0);
  db->delete_transcript(id);
  page->reloadTranscript(id, "Session");
  ASSERT_EQ(activated.count(), 1);
  EXPECT_EQ(activated.first().first().toLongLong(), 0);
}

TEST_F(TranscriptPageTest, ShowsNewestTranscriptWithPhrasesInOrder) {
  EXPECT_EQ(page->transcriptCount(), 1);
  auto labels = page->findChildren<QLabel *>("phraseText");
  ASSERT_EQ(labels.size(), 2); EXPECT_EQ(labels[0]->text(), "First");
  EXPECT_EQ(labels[1]->text(), "Second");
  if (qEnvironmentVariableIsSet("SESSIO_TRANSCRIPT_SCREENSHOT")) {
    db->update_transcript_phrase_text(first,
        "На следующей встрече предлагаю вернуться к этому вопросу и обсудить, какие изменения удалось заметить за неделю.");
    db->update_transcript_phrase_text(second, "Хорошо. Я запишу несколько примеров, чтобы мы могли обсудить их подробнее.");
    page->reload(event, QString::fromUtf8("Тестовая сессия"));
    auto *storage = new CallNotesController(db, page.get());
    storage->openTranscript(id);
    storage->setManualText(QString::fromUtf8("**Наблюдения:** обсудить изменения за неделю.\n\nВернуться к примерам на следующей встрече."));
    auto *notes = new CallNotesPage(db, storage);
    notes->setMeeting(QString::fromUtf8("Тестовая сессия · 60 минут"), "ended");
    notes->editor()->setCallNoteActions(storage->materialId(), {
      [storage] { return storage->noteEntries(); },
      [storage](const QString &text, const QStringList &paths, const QString &entry) { return storage->saveNoteEntry(text, paths, entry); },
      [storage](const QString &entry, const QString &attachment) { return storage->noteAttachmentPath(entry, attachment); },
      [storage] { return storage->manualText(); },
      [storage](const QString &text) { return storage->setManualText(text); }
    });
    notes->setAgreements(QJsonArray{QJsonObject{{"text", QString::fromUtf8("Подготовить несколько примеров к следующей встрече")}, {"active", true}}});
    page->setNotesView(notes);
    page->resize(1280, 860); page->show(); QTest::qWait(20);
    page->findChild<QWidget *>("phraseCard_" + QString::number(first))->setFocus();
    QApplication::processEvents();
    page->grab().save(qEnvironmentVariable("SESSIO_TRANSCRIPT_SCREENSHOT"));
    page->resize(680, 860); QApplication::processEvents();
    page->grab().save(qEnvironmentVariable("SESSIO_TRANSCRIPT_SCREENSHOT") + "-narrow.png");
    page->resize(1280, 860);
    button("editPhrase_" + QString::number(first))->click();
    QApplication::processEvents();
    page->grab().save(qEnvironmentVariable("SESSIO_TRANSCRIPT_SCREENSHOT") + "-edit.png");
  }
}
TEST_F(TranscriptPageTest, MissingModelDoesNotLeaveAnEmptyMetadataField) {
  const auto newer = db->add_transcript(event, "live_local_v1", std::nullopt);
  db->set_transcript_status(newer, "draft");
  page->reload(event, "Session");
  EXPECT_FALSE(page->findChild<QLabel *>("transcriptMetadata")->text().contains("Model:"));
}
TEST_F(TranscriptPageTest, KeyboardSelectionRevealsPhraseActionsWithoutHover) {
  page->resize(1100, 800);
  page->show();
  QApplication::processEvents();
  auto *card = page->findChild<QWidget *>("phraseCard_" + QString::number(first));
  ASSERT_NE(card, nullptr);
  auto *actions = card->findChild<QWidget *>("phraseActions");
  ASSERT_NE(actions, nullptr);
  page->findChild<QPushButton *>("backToEvent")->setFocus();
  QApplication::processEvents();
  EXPECT_TRUE(actions->isHidden());
  card->setFocus(Qt::TabFocusReason);
  QApplication::processEvents();
  EXPECT_FALSE(actions->isHidden());
  button("editPhrase_" + QString::number(first))->click();
  EXPECT_TRUE(page->editing());
  ASSERT_NE(page->findChild<QPlainTextEdit *>("phraseEditor"), nullptr);
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
TEST_F(TranscriptPageTest, HeaderShowsIndependentTranscriptClientsAndSafeEmptyFallback) {
  EXPECT_EQ(page->findChild<QLabel *>("transcriptClient")->text(), "No clients attached");
  DuckClient client;
  client.name = "Test"; client.last_name = "Client";
  const auto clientId = db->add_client(client);
  db->add_event_client(event, clientId);
  page->reload(event, "Session");
  EXPECT_EQ(page->findChild<QLabel *>("transcriptClient")->text(), "No clients attached");
  ASSERT_TRUE(db->set_transcript_clients(id, {clientId}));
  page->reload(event, "Session");
  EXPECT_EQ(page->findChild<QLabel *>("transcriptClient")->text(), "Clients: Test Client");
  ASSERT_TRUE(db->set_transcript_clients(id, {}));
  page->reload(event, "Session");
  EXPECT_EQ(page->findChild<QLabel *>("transcriptClient")->text(), "No clients attached");
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
  const auto labels = page->findChildren<QLabel *>("phraseTime");
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
  QApplication app(argc, argv);
  if (qEnvironmentVariableIsSet("SESSIO_PREVIEW_THEME")) {
    auto *style = new oclero::qlementine::QlementineStyle(&app);
    style->setThemeJsonPath(qEnvironmentVariable("SESSIO_PREVIEW_THEME"));
    app.setStyle(style);
  }
  ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS();
}
