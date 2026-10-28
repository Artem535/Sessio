#include "client_notes_page.h"
#include <QApplication>
#include <QAction>
#include <QJsonObject>
#include <QFile>
#include <QLineEdit>
#include <QComboBox>
#include <QImage>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTextBrowser>
#include <QToolButton>
#include <QQuickWidget>
#include <QQuickItem>
#include <QMenu>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>
#include <memory>
#include <oclero/qlementine/style/QlementineStyle.hpp>

namespace {
TEST(ClientJournalTest, QuickWorkspaceLoadsAndSavesOnlyForSelectedParticipant) {
  QTemporaryDir temp;
  pcm::config::Config config{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(temp.path().toStdString())}};
  auto db = std::make_shared<pcm::database::Database>(config);
  DuckClient first; first.name = "First synthetic"; first.id = db->add_client(first);
  DuckClient second; second.name = "Second synthetic"; second.id = db->add_client(second);
  ClientNotesPage page(db);
  page.enableQuickWorkspace();
  page.resize(1280, 850);
  page.show();
  auto *quick = page.findChild<QQuickWidget *>("clientNotesQuickWorkspace");
  ASSERT_NE(quick, nullptr);
  ASSERT_EQ(quick->status(), QQuickWidget::Ready);
  ASSERT_NE(quick->rootObject(), nullptr);
  auto *header = page.headerControls();
  EXPECT_EQ(header, page.headerControls());
  EXPECT_TRUE(quick->rootObject()->property("externalHeader").toBool());
  auto *participant = header->findChild<QToolButton *>("notesHeaderParticipant");
  ASSERT_NE(participant, nullptr);
  ASSERT_NE(participant->menu(), nullptr);
  ASSERT_EQ(participant->menu()->actions().size(), 2);
  const auto initialActions = participant->menu()->actions();
  page.refresh();
  EXPECT_EQ(participant->menu()->actions(), initialActions);
  EXPECT_EQ(page.participants().size(), 2);
  page.selectParticipant(QString::number(first.id));
  EXPECT_EQ(participant->text(), "First synthetic");
  EXPECT_TRUE(participant->menu()->actions().first()->isChecked());
  page.setDraft("A persisted note");
  auto *editor = quick->rootObject()->findChild<QQuickItem *>("qmlPersistentNoteEditor");
  ASSERT_NE(editor, nullptr);
  EXPECT_EQ(editor->property("text").toString(), "A persisted note");
  page.saveWorkspaceNote();
  ASSERT_EQ(db->get_client_notes(first.id).size(), 1);
  DuckClientNoteAttachment attachment;
  attachment.note_id = db->get_client_notes(first.id).front().id;
  attachment.file_name = "saved.txt";
  attachment.relative_path = "synthetic/saved.txt";
  ASSERT_GT(db->add_client_note_attachment(attachment), 0);
  page.refresh();
  ASSERT_EQ(page.workspaceDocument().value("attachments").toList().size(), 1);
  EXPECT_EQ(page.workspaceDocument().value("attachments").toList().first().toMap().value("name").toString(), "saved.txt");
  EXPECT_TRUE(page.draft().isEmpty());
  EXPECT_TRUE(editor->property("text").toString().isEmpty());
  EXPECT_EQ(page.journal().size(), 1);
  page.selectParticipant(QString::number(second.id));
  EXPECT_TRUE(page.journal().isEmpty());
  EXPECT_TRUE(db->get_client_notes(second.id).empty());
  page.setDraft("Second participant draft");
  page.refresh();
  EXPECT_EQ(page.draft(), "Second participant draft");
  EXPECT_EQ(editor->property("text").toString(), "Second participant draft");
  if (qEnvironmentVariableIsSet("SESSIO_QML_JOURNAL_SCREENSHOT")) {
    QTest::qWait(150);
    page.grab().save(qEnvironmentVariable("SESSIO_QML_JOURNAL_SCREENSHOT"));
  }
}

TEST(ClientJournalTest, RepeatedCallsDoNotCreateEmptyNoteSectionsOrRepeatedPrimaryActions) {
  QTemporaryDir temp;
  pcm::config::Config config{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(temp.path().toStdString())}};
  auto db = std::make_shared<pcm::database::Database>(config);
  DuckClient client; client.name = "Synthetic"; client.id = db->add_client(client);
  DuckEvent event; event.name = "Repeated consultation"; event.start_date = 1730000000000;
  event.end_date = *event.start_date + 3600000; event.duration = 3600;
  event.event_stat_id = 1; event.payment_stat_id = 1;
  event.id = db->add_event(event); db->add_event_client(event.id, client.id);
  for (int i = 0; i < 8; ++i) {
    const auto transcript = db->add_transcript(event.id, "live_local_v1");
    const auto material = db->create_call_material(event.id);
    ASSERT_TRUE(db->link_call_material_transcript(material, transcript));
    ASSERT_TRUE(db->save_call_material(material, db->get_call_material(material).revision, R"({"ended":true})"));
  }
  ClientNotesPage page(db); page.setClientInfo(client);
  page.resize(1280, 860); page.show(); QApplication::processEvents();
  EXPECT_EQ(page.findChildren<QWidget *>("notesJournalOwnNotesSection").size(), 0);
  EXPECT_EQ(page.findChildren<QPushButton *>("notesJournalEditMeeting").size(), 1);
  EXPECT_EQ(page.findChildren<QPushButton *>("notesJournalOpenTranscript").size(), 1);
  auto *more = page.findChild<QToolButton *>("notesJournalMore");
  ASSERT_NE(more, nullptr); ASSERT_NE(more->menu(), nullptr);
  EXPECT_EQ(more->menu()->windowFlags() & Qt::WindowType_Mask, Qt::Popup);
  EXPECT_TRUE(more->menu()->isHidden());
  const auto actions = page.findChildren<QAction *>("notesJournalMaterialAction");
  EXPECT_EQ(actions.size(), 8);
  QSignalSpy opened(&page, &ClientNotesPage::openCallMaterialRequested);
  for (auto *action : actions) action->trigger();
  EXPECT_EQ(opened.count(), 8);
  const auto materials = page.workspaceDocument().value("materials").toList();
  const auto transcripts = page.workspaceDocument().value("transcripts").toList();
  ASSERT_EQ(materials.size(), 8);
  ASSERT_EQ(transcripts.size(), 8);
  opened.clear();
  QSignalSpy transcriptOpened(&page, &ClientNotesPage::openTranscriptRequested);
  for (const auto &material : materials) page.openWorkspaceMeeting(material.toMap().value("id").toString());
  for (const auto &transcript : transcripts) page.openWorkspaceTranscript(transcript.toMap().value("id").toString());
  EXPECT_EQ(opened.count(), 8);
  EXPECT_EQ(transcriptOpened.count(), 8);
  if (qEnvironmentVariableIsSet("SESSIO_JOURNAL_SCREENSHOT"))
    page.grab().save(qEnvironmentVariable("SESSIO_JOURNAL_SCREENSHOT") + "-repeated.png");
}

TEST(ClientJournalTest, GroupsOldSessionNotesAndCallMaterialsAndCombinesCategoryWithSearch) {
  QTemporaryDir temp;
  pcm::config::Config config{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(temp.path().toStdString())}};
  auto db = std::make_shared<pcm::database::Database>(config);
  DuckClient client; client.name = "Synthetic"; client.id = db->add_client(client);
  DuckEvent event; event.name = "Older session"; event.start_date = 1000000000000;
  event.end_date = *event.start_date + 3600000; event.duration = 3600;
  event.event_stat_id = 1; event.payment_stat_id = 1;
  event.id = db->add_event(event); ASSERT_GT(db->add_event_client(event.id, client.id), 0);
  const auto transcript = db->add_transcript(event.id, "live_local_v1");
  const auto material = db->create_call_material(event.id);
  ASSERT_TRUE(db->link_call_material_transcript(material, transcript));
  ASSERT_TRUE(db->save_call_material(material, db->get_call_material(material).revision,
      R"({"ended":true,"manual_entries":[{"id":"one","body_markdown":"Specialist protocol","attachments":[{"id":"image","file_name":"outline.png","mime_type":"image/png"}]}],"final":{"note":{"text":"Meeting summary"}}})"));
  DuckClientNote note; note.client_id = client.id; note.linked_event_id = event.id;
  note.body_markdown = "Linked observation"; ASSERT_GT(db->add_client_note(note), 0);
  note.linked_event_id.reset(); note.body_markdown = "Free observation"; ASSERT_GT(db->add_client_note(note), 0);
  ClientNotesPage page(db); page.setClientInfo(client);
  QImage image(16, 16, QImage::Format_RGB32); image.fill(Qt::blue);
  const auto path = temp.filePath("outline.png"); ASSERT_TRUE(image.save(path));
  int attachmentReads = 0;
  page.setMaterialAttachmentResolver([&](qint64 requestedMaterial, const QString &entry, const QString &attachment) {
    EXPECT_EQ(requestedMaterial, material); EXPECT_EQ(entry, "one"); EXPECT_EQ(attachment, "image");
    ++attachmentReads; return path;
  });
  auto *history = page.findChild<QListWidget *>("notesJournalList"); ASSERT_NE(history, nullptr);
  ASSERT_EQ(history->count(), 2);
  auto *search = page.findChild<QLineEdit *>("notesJournalSearch");
  auto *filter = page.findChild<QComboBox *>("notesJournalFilter");
  filter->setCurrentIndex(1); search->setText("Specialist protocol");
  ASSERT_GE(history->currentRow(), 0);
  EXPECT_TRUE(history->currentItem()->text().contains("Older session"));
  QSignalSpy opened(&page, &ClientNotesPage::openTranscriptRequested);
  auto buttons = page.findChildren<QPushButton *>("notesJournalOpenTranscript");
  auto found = std::find_if(buttons.begin(), buttons.end(), [](auto *b) { return !b->isHidden(); });
  ASSERT_NE(found, buttons.end()); (*found)->click();
  ASSERT_EQ(opened.count(), 1); EXPECT_EQ(opened.first().first().toLongLong(), transcript);
  auto previews = page.findChildren<QPushButton *>("notesJournalPreviewAttachment");
  ASSERT_FALSE(previews.isEmpty()); auto *previewButton = previews.last(); previewButton->click();
  EXPECT_EQ(attachmentReads, 1);
  auto *preview = previewButton->parentWidget()->findChild<QLabel *>("notesJournalAttachmentPreview");
  ASSERT_NE(preview, nullptr); EXPECT_FALSE(preview->pixmap().isNull());
  filter->setCurrentIndex(2);
  EXPECT_EQ(history->currentRow(), -1);
  search->clear();
  ASSERT_GE(history->currentRow(), 0);
  EXPECT_TRUE(history->currentItem()->text().contains("Free observation"));
  EXPECT_TRUE(page.journalMarkdown().contains("Linked observation"));
  EXPECT_TRUE(page.journalMarkdown().contains("Specialist protocol"));
  EXPECT_TRUE(page.journalMarkdown().contains("Meeting summary"));
  page.resize(2360, 1400); page.show(); QApplication::processEvents();
  auto *paper = page.findChild<QWidget *>("notesJournalPaper");
  ASSERT_NE(paper, nullptr);
  EXPECT_GT(paper->width(), page.width() - 400);
  EXPECT_LT(paper->mapTo(&page, QPoint()).x(), 400);
  page.findChild<QPushButton *>("notesJournalNew")->click();
  QTest::qWait(80);
  auto *composer = page.findChild<QPlainTextEdit *>("notesComposer");
  ASSERT_NE(composer, nullptr);
  EXPECT_LE(composer->height(), 220);
  EXPECT_GT(composer->parentWidget()->width(), page.width() - 400);
  if (qEnvironmentVariableIsSet("SESSIO_JOURNAL_SCREENSHOT")) {
    search->clear(); filter->setCurrentIndex(1);
    page.resize(2360, 1400); page.show(); QApplication::processEvents();
    page.grab().save(qEnvironmentVariable("SESSIO_JOURNAL_SCREENSHOT"));
    page.resize(560, 860); QTest::qWait(80);
    page.grab().save(qEnvironmentVariable("SESSIO_JOURNAL_SCREENSHOT") + "-narrow.png");
  }
}

TEST(ClientJournalTest, SelectingAndSearchingEntriesPreservesTheUnsentNote) {
  QTemporaryDir temp;
  pcm::config::Config config{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(temp.path().toStdString())}};
  auto db = std::make_shared<pcm::database::Database>(config);
  DuckClient client; client.name = "Synthetic"; client.id = db->add_client(client);
  for (const auto &text : {"First outline", "Second observation"}) {
    DuckClientNote note; note.client_id = client.id; note.body_markdown = text;
    note.created_at = text[0] == 'F' ? 1000 : 2000;
    ASSERT_GT(db->add_client_note(note), 0);
  }
  ClientNotesPage page(db); page.setClientInfo(client);
  auto *history = page.findChild<QListWidget *>("notesJournalList");
  auto *search = page.findChild<QLineEdit *>("notesJournalSearch");
  ASSERT_NE(history, nullptr); ASSERT_NE(search, nullptr);
  ASSERT_EQ(history->count(), 2);
  auto *composer = page.findChild<QPlainTextEdit *>("notesComposer");
  composer->setPlainText("Unsent note");
  search->setText("outline");
  EXPECT_TRUE(history->item(0)->isHidden());
  EXPECT_FALSE(history->item(1)->isHidden());
  EXPECT_EQ(history->currentRow(), 1);
  EXPECT_TRUE(page.journalMarkdown().contains("Second observation"));
  EXPECT_EQ(composer->toPlainText(), "Unsent note");
  search->setText("no matching text");
  EXPECT_EQ(history->currentRow(), -1);
  search->clear();
  EXPECT_FALSE(history->item(0)->isHidden());
  EXPECT_EQ(composer->toPlainText(), "Unsent note");
}

class ClientNotesUiTest : public ::testing::Test {
protected:
  ClientNotesPage page{nullptr};
  QJsonArray entries;
  QString manual;
  bool fail = false;
  int writes = 0;
  QStringList savedPaths;
  QStringList draftPaths;
  bool failDraftPaths = false;
  int draftPathWrites = 0;
  std::function<QStringList(const QStringList &)> canonicalizePaths;
  void bind() {
    page.setCallNoteActions(7, {
      [this] { return entries; },
      [this](const QString &text, const QStringList &paths, const QString &id) {
        ++writes;
        savedPaths = paths;
        if (fail) return QString{};
        const auto savedId = id.isEmpty() ? QString::number(entries.size() + 1) : id;
        QJsonObject entry{{"id", savedId}, {"body_markdown", text}, {"created_at", 1234}};
        bool updated = false;
        for (qsizetype i = 0; i < entries.size(); ++i) {
          if (entries[i].toObject()["id"].toString() == savedId) {
            entry["attachments"] = entries[i].toObject()["attachments"];
            entries[i] = entry; updated = true; break;
          }
        }
        if (!updated) entries.append(entry);
        if (id.isEmpty()) { manual.clear(); draftPaths.clear(); }
        // The real controller emits changed synchronously during saving.
        page.refresh();
        return savedId;
      }, {}, [this] { return manual; },
      [this](const QString &text) { manual = text; return true; },
      [this] { return draftPaths; },
      [this](const QStringList &paths) {
        ++draftPathWrites;
        if (failDraftPaths) return false;
        draftPaths = canonicalizePaths ? canonicalizePaths(paths) : paths;
        page.refresh(); return true;
      }
    });
  }
  QPlainTextEdit *editor() { return page.findChild<QPlainTextEdit *>("notesComposer"); }
  QPushButton *save() { return page.findChild<QPushButton *>("notesSave"); }
};

TEST_F(ClientNotesUiTest, CallWithoutClientSavesSeparateEntriesOnlyOnExplicitSave) {
  bind();
  editor()->setPlainText("**First**");
  EXPECT_EQ(writes, 0);
  EXPECT_EQ(manual, "**First**");
  save()->click();
  editor()->setPlainText("Second"); save()->click();
  ASSERT_EQ(entries.size(), 2);
  EXPECT_EQ(entries[0].toObject()["body_markdown"].toString(), "**First**");
  EXPECT_TRUE(editor()->toPlainText().isEmpty());
  EXPECT_TRUE(manual.isEmpty());
}

TEST_F(ClientNotesUiTest, FailedSaveKeepsTheDraftAndDoesNotSignalSuccess) {
  bind(); fail = true;
  QSignalSpy saved(&page, &ClientNotesPage::noteEntrySaved);
  editor()->setPlainText("Keep this text"); save()->click();
  EXPECT_EQ(editor()->toPlainText(), "Keep this text");
  EXPECT_EQ(saved.count(), 0);
  EXPECT_TRUE(entries.isEmpty());
}

TEST_F(ClientNotesUiTest, AttachmentPathsSurviveFailureAndAreRetriedOnExplicitSave) {
  bind(); QTemporaryDir dir;
  const auto path = dir.filePath("outline.md");
  QFile file(path); ASSERT_TRUE(file.open(QIODevice::WriteOnly)); file.write("outline"); file.close();
  page.addPendingAttachments({path});
  fail = true; save()->click();
  EXPECT_EQ(savedPaths, QStringList{path});
  auto *pending = page.findChild<QListWidget *>("notesPendingAttachments"); ASSERT_NE(pending, nullptr);
  EXPECT_EQ(pending->count(), 1);
  fail = false; save()->click();
  EXPECT_EQ(savedPaths, QStringList{path}); EXPECT_EQ(pending->count(), 0);
  ASSERT_EQ(entries.size(), 1);
}

TEST_F(ClientNotesUiTest, DraftAttachmentsUseOwnedPathsAndCanBeReopenedAndRemoved) {
  bind(); QTemporaryDir dir;
  const auto source = dir.filePath("source.md");
  const auto owned = dir.filePath("owned.md");
  QFile file(source); ASSERT_TRUE(file.open(QIODevice::WriteOnly)); file.write("outline"); file.close();
  ASSERT_TRUE(QFile::copy(source, owned));
  canonicalizePaths = [owned](const QStringList &paths) { return paths.isEmpty() ? QStringList{} : QStringList{owned}; };
  page.addPendingAttachments({source});
  EXPECT_EQ(draftPaths, QStringList{owned});
  auto *pending = page.findChild<QListWidget *>("notesPendingAttachments");
  ASSERT_EQ(pending->count(), 1);
  EXPECT_TRUE(pending->item(0)->text().contains("owned.md"));
  ClientNotesPage reopened(nullptr);
  reopened.setCallNoteActions(7, {{}, {}, {}, {}, {}, [this] { return draftPaths; }, {}});
  ASSERT_EQ(reopened.findChild<QListWidget *>("notesPendingAttachments")->count(), 1);
  EXPECT_TRUE(reopened.findChild<QListWidget *>("notesPendingAttachments")->item(0)->text().contains("owned.md"));
  pending->itemDoubleClicked(pending->item(0));
  EXPECT_TRUE(draftPaths.isEmpty()); EXPECT_EQ(pending->count(), 0);
}

TEST_F(ClientNotesUiTest, FailedDraftAttachmentPersistenceKeepsSelectionsAndReportsFailure) {
  bind(); QTemporaryDir dir;
  const auto source = dir.filePath("outline.md");
  QFile file(source); ASSERT_TRUE(file.open(QIODevice::WriteOnly)); file.write("outline"); file.close();
  failDraftPaths = true; page.addPendingAttachments({source}); page.refresh();
  auto *pending = page.findChild<QListWidget *>("notesPendingAttachments"); ASSERT_EQ(pending->count(), 1);
  EXPECT_TRUE(draftPaths.isEmpty());
  auto *status = page.findChild<QLabel *>("notesSaveStatus"); ASSERT_NE(status, nullptr);
  EXPECT_EQ(status->text(), ClientNotesPage::tr("Could not save — keep this page open"));
  pending->itemDoubleClicked(pending->item(0));
  EXPECT_EQ(pending->count(), 1);
}

TEST_F(ClientNotesUiTest, EntryEditAttachmentsNeverReplaceNewDraftAttachments) {
  bind(); editor()->setPlainText("Saved entry"); save()->click();
  QTemporaryDir dir;
  const auto draftFile = dir.filePath("draft.md"), editFile = dir.filePath("edit.md");
  for (const auto &path : {draftFile, editFile}) {
    QFile file(path); ASSERT_TRUE(file.open(QIODevice::WriteOnly)); file.write("outline");
  }
  editor()->setPlainText("New draft"); page.addPendingAttachments({draftFile});
  const int previousWrites = draftPathWrites;
  page.findChild<QPushButton *>("notesEditEntry")->click();
  page.addPendingAttachments({editFile});
  EXPECT_EQ(draftPathWrites, previousWrites);
  EXPECT_EQ(draftPaths, QStringList{draftFile});
  manual += "\n\nAccepted AI"; page.refresh();
  page.findChild<QPushButton *>("notesCancelEdit")->click();
  EXPECT_EQ(editor()->toPlainText(), "New draft\n\nAccepted AI");
  auto *pending = page.findChild<QListWidget *>("notesPendingAttachments"); ASSERT_EQ(pending->count(), 1);
  EXPECT_TRUE(pending->item(0)->text().contains("draft.md"));
}

TEST_F(ClientNotesUiTest, AiAppendAndHistoryRefreshPreserveUndoAndExistingEntryWidgets) {
  bind(); editor()->setPlainText("History"); save()->click();
  auto *entryBody = page.findChild<QTextBrowser *>("notesEntryBody");
  editor()->setPlainText("My text");
  manual += "\n\nAI draft"; page.refresh();
  EXPECT_EQ(editor()->toPlainText(), "My text\n\nAI draft");
  EXPECT_EQ(page.findChild<QTextBrowser *>("notesEntryBody"), entryBody);
  editor()->undo(); EXPECT_EQ(editor()->toPlainText(), "My text");
  EXPECT_EQ(entries.size(), 1);
}

TEST_F(ClientNotesUiTest, EditingUpdatesTheSelectedEntryAndCancelRestoresUnsentText) {
  bind(); editor()->setPlainText("First"); save()->click();
  editor()->setPlainText("Unsent note");
  page.findChild<QPushButton *>("notesEditEntry")->click();
  editor()->setPlainText("Revision");
  page.findChild<QPushButton *>("notesCancelEdit")->click();
  EXPECT_EQ(editor()->toPlainText(), "Unsent note");
  page.findChild<QPushButton *>("notesEditEntry")->click();
  editor()->setPlainText("Revision"); save()->click();
  ASSERT_EQ(entries.size(), 1);
  EXPECT_EQ(entries[0].toObject()["body_markdown"].toString(), "Revision");
  EXPECT_EQ(manual, "Unsent note");
  EXPECT_EQ(editor()->toPlainText(), manual);
}

TEST_F(ClientNotesUiTest, EditingSavedEntryNeverReplacesThePersistedNewDraft) {
  bind(); editor()->setPlainText("Saved entry"); save()->click();
  editor()->setPlainText("Persistent new draft");
  page.findChild<QPushButton *>("notesEditEntry")->click();
  editor()->setPlainText("Unsaved entry revision");
  EXPECT_EQ(manual, "Persistent new draft");
  ClientNotesPage reopened(nullptr);
  reopened.setCallNoteActions(7, {{}, {}, {}, [this] { return manual; }, {}});
  EXPECT_EQ(reopened.findChild<QPlainTextEdit *>("notesComposer")->toPlainText(), "Persistent new draft");
}

TEST_F(ClientNotesUiTest, AiAppendDuringEntryEditingStaysInNewDraftAndSurvivesCancel) {
  bind(); editor()->setPlainText("Saved entry"); save()->click();
  editor()->setPlainText("New draft");
  page.findChild<QPushButton *>("notesEditEntry")->click();
  editor()->setPlainText("Entry revision");
  manual += "\n\nAccepted AI text"; page.refresh();
  EXPECT_EQ(editor()->toPlainText(), "Entry revision");
  page.findChild<QPushButton *>("notesCancelEdit")->click();
  EXPECT_EQ(editor()->toPlainText(), "New draft\n\nAccepted AI text");
  EXPECT_EQ(manual, editor()->toPlainText());
}

TEST_F(ClientNotesUiTest, AiAppendDuringEntryEditingSurvivesSavingTheEntry) {
  bind(); editor()->setPlainText("Saved entry"); save()->click();
  editor()->setPlainText("New draft");
  page.findChild<QPushButton *>("notesEditEntry")->click();
  editor()->setPlainText("Entry revision");
  manual += "\n\nAccepted AI text"; page.refresh(); save()->click();
  EXPECT_EQ(entries[0].toObject()["body_markdown"].toString(), "Entry revision");
  EXPECT_EQ(manual, "New draft\n\nAccepted AI text");
  EXPECT_EQ(editor()->toPlainText(), manual);
}

TEST_F(ClientNotesUiTest, RebindingTheCallResetsEditingPreviewAndObsoleteCancelAction) {
  bind(); editor()->setPlainText("Saved entry"); save()->click();
  page.findChild<QPushButton *>("notesEditEntry")->click();
  page.findChild<QPushButton *>("notesPreviewToggle")->click();
  QString nextDraft = "Next call draft";
  page.setCallNoteActions(8, {{}, {}, {}, [&nextDraft] { return nextDraft; },
      [&nextDraft](const QString &text) { nextDraft = text; return true; }});
  auto *cancel = page.findChild<QPushButton *>("notesCancelEdit");
  EXPECT_TRUE(cancel->isHidden());
  EXPECT_FALSE(page.findChild<QPushButton *>("notesPreviewToggle")->isChecked());
  EXPECT_TRUE(page.findChild<QTextBrowser *>("notesPreview")->isHidden());
  EXPECT_EQ(save()->text(), ClientNotesPage::tr("Add note"));
  cancel->click();
  EXPECT_EQ(editor()->toPlainText(), "Next call draft");
  EXPECT_EQ(nextDraft, "Next call draft");
}

TEST_F(ClientNotesUiTest, ClearingTextOnAnEntryWithStoredAttachmentsDelegatesValidationToStorage) {
  entries.append(QJsonObject{{"id", "with-file"}, {"body_markdown", "Remove this text"},
      {"attachments", QJsonArray{QJsonObject{{"id", "file-id"}, {"file_name", "outline.pdf"}}}}});
  bind();
  page.findChild<QPushButton *>("notesEditEntry")->click();
  editor()->clear(); save()->click();
  EXPECT_EQ(writes, 1);
  ASSERT_EQ(entries.size(), 1);
  EXPECT_EQ(entries[0].toObject()["id"].toString(), "with-file");
  EXPECT_TRUE(entries[0].toObject()["body_markdown"].toString().isEmpty());
  EXPECT_EQ(entries[0].toObject()["attachments"].toArray().size(), 1);
}

TEST_F(ClientNotesUiTest, PreviewAndRefreshPreserveMarkdownAndCursorAtCompactWidths) {
  bind(); page.show();
  for (int width : {345, 440, 490}) {
    page.resize(width, 850); QCoreApplication::processEvents();
    editor()->setPlainText("**My text**\n\nAI proposal");
    editor()->setFocus(); auto cursor = editor()->textCursor(); cursor.setPosition(3); editor()->setTextCursor(cursor);
    page.refresh();
    EXPECT_EQ(editor()->textCursor().position(), 3);
    auto *preview = page.findChild<QPushButton *>("notesPreviewToggle"); preview->click();
    EXPECT_EQ(page.findChild<QTextBrowser *>("notesPreview")->toPlainText(), "My text\nAI proposal");
    preview->click();
    EXPECT_LE(page.minimumSizeHint().width(), width);
    EXPECT_EQ(page.width(), width);
    EXPECT_EQ(editor()->toPlainText(), "**My text**\n\nAI proposal");
  }
}

TEST_F(ClientNotesUiTest, CallComposerPrecedesTheAiAndHistoryFeed) {
  bind();
  auto *feed = page.findChild<QScrollArea *>(); ASSERT_NE(feed, nullptr);
  EXPECT_LT(page.layout()->indexOf(editor()->parentWidget()), page.layout()->indexOf(feed->parentWidget()));
}

TEST_F(ClientNotesUiTest, EmbeddedPageShowsOnlyThisMeetingWithoutOwnScroll) {
  page.setEmbedded(true);
  entries.append(QJsonObject{{"id", "e1"}, {"body_markdown", "First"}, {"created_at", 1}});
  bind();
  DuckClient client; client.id = 1;
  page.setClientInfo(client);
  page.resize(360, 600); page.show(); QCoreApplication::processEvents();
  for (auto *label : page.findChildren<QLabel *>()) {
    EXPECT_NE(label->text(), "Client history");
    EXPECT_NE(label->text(), "This meeting");
  }
  auto *scroll = page.findChild<QScrollArea *>();
  ASSERT_NE(scroll, nullptr);
  EXPECT_EQ(scroll->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
  auto *body = page.findChild<QTextBrowser *>("notesEntryBody");
  ASSERT_NE(body, nullptr);
  EXPECT_GE(page.minimumHeight(), body->height());
}
} // namespace

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  auto style = std::make_unique<oclero::qlementine::QlementineStyle>(&app);
  if (qEnvironmentVariableIsSet("SESSIO_PREVIEW_THEME"))
    style->setThemeJsonPath(qEnvironmentVariable("SESSIO_PREVIEW_THEME"));
  app.setStyle(style.release()); // QApplication owns the style
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
