#include "call_notes_page.h"
#include "agreements_widget.h"
#include "ai_summary_widget.h"
#include "call_notes_controller.h"
#include "client_notes_page.h"
#include "key_points_widget.h"
#include "meeting_history_widgets.h"
#include "notes_workspace_bridge.h"
#include <QQuickWidget>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QTextDocument>
#include <QTextCursor>
#include <QTest>
#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QScrollBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <gtest/gtest.h>

// Produces a controller with one ready live draft and one key point.
struct NotesFixture {
  QTemporaryDir dir;
  pcm::config::Config conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir.path().toStdString())}};
  std::shared_ptr<pcm::database::Database> db = std::make_shared<pcm::database::Database>(conf);
  CallNotesController controller{db};
  DuckTranscriptPhrase phrase;
  void generate(const QString &text = QStringLiteral("Prepare a short outline."),
                const QString &pointText = QStringLiteral("Outline is due")) {
    const auto id = db->add_transcript(std::nullopt, "live_local_v1");
    controller.beginCall(); controller.attachTranscript(id); controller.setGenerationEnabled(true);
    phrase.transcript_id = id; phrase.track_role = "participant";
    phrase.speaker_name = "Participant"; phrase.text = "I will prepare a short outline.";
    phrase.start_ms = 73000; phrase.end_ms = 76000;
    phrase.id = db->add_transcript_phrase(phrase);
    QSignalSpy requests(&controller, &CallNotesController::requestReady);
    controller.addPhrase(phrase); controller.flush();
    ASSERT_FALSE(requests.isEmpty());
    auto reply = requests.last().first().toJsonObject();
    const QJsonObject note{{"text", text}, {"evidence_ids", QJsonArray{QString::number(phrase.id)}}};
    auto point = note; point["text"] = pointText;
    reply["notes"] = QJsonObject{{"note", note}, {"key_points", QJsonArray{point}}};
    controller.handleResult(reply);
    ASSERT_EQ(controller.draft()["text"].toString(), text);
  }
};

class CallNotesPageTest : public ::testing::Test, public NotesFixture {};

TEST_F(CallNotesPageTest, ModalSessionBuffersChangesAndSavePreservesIndependentDraft) {
  controller.beginCall();
  const auto id = controller.saveNoteEntry("Saved note");
  controller.setManualText("Independent draft");
  NotesWorkspaceBridge bridge(db, &controller, nullptr);
  bridge.beginEditorSession();
  EXPECT_TRUE(bridge.editorNoteId().isEmpty());
  ASSERT_TRUE(bridge.selectEditorNote(id));
  bridge.format(0, 5, "bold");
  EXPECT_TRUE(bridge.editorDirty());
  EXPECT_EQ(controller.noteEntries().first().toObject()["body_markdown"].toString(), "Saved note");
  EXPECT_FALSE(bridge.selectEditorNote({}));
  ASSERT_TRUE(bridge.saveEditorSession());
  EXPECT_EQ(controller.manualText(), "Independent draft");
  EXPECT_TRUE(controller.noteEntries().first().toObject()["body_markdown"].toString().contains("**Saved**"));
}

TEST_F(CallNotesPageTest, ClosingAnUnsavedModalSessionLeavesStorageUntouched) {
  controller.beginCall(); controller.setManualText("Original draft");
  {
    NotesWorkspaceBridge bridge(db, &controller, nullptr);
    bridge.beginEditorSession(); bridge.format(0, 8, "bold");
    EXPECT_TRUE(bridge.editorDirty());
    EXPECT_EQ(controller.manualText(), "Original draft");
  }
  EXPECT_EQ(controller.manualText(), "Original draft");
  EXPECT_TRUE(controller.noteEntries().isEmpty());
}

TEST_F(CallNotesPageTest, ModalNewNoteRefusesToClearAConcurrentlyChangedDraft) {
  controller.beginCall(); controller.setManualText("Original draft");
  NotesWorkspaceBridge bridge(db, &controller, nullptr); bridge.beginEditorSession();
  bridge.format(0, 8, "bold");
  controller.setManualText("Updated elsewhere");
  EXPECT_FALSE(bridge.saveEditorSession());
  EXPECT_EQ(controller.manualText(), "Updated elsewhere");
  EXPECT_TRUE(bridge.editorDirty());
  EXPECT_TRUE(controller.noteEntries().isEmpty());
}

TEST_F(CallNotesPageTest, ModalSaveRemainsBoundToTheMeetingWhenANewCallStarts) {
  generate(); const auto originalMaterial = controller.materialId();
  NotesWorkspaceBridge bridge(db, &controller, nullptr); bridge.beginEditorSession();
  bridge.appendSummary();
  ASSERT_TRUE(bridge.editorDirty());
  controller.beginCall();
  const auto nextMaterial = controller.materialId();
  ASSERT_NE(nextMaterial, originalMaterial);
  ASSERT_TRUE(bridge.saveEditorSession());
  EXPECT_TRUE(controller.noteEntries().isEmpty());
  CallNotesController stored(db); stored.openMaterial(originalMaterial);
  EXPECT_EQ(stored.noteEntries().size(), 1);
}

TEST_F(CallNotesPageTest, ModalQmlUsesDedicatedEditorAndAllDismissalPathsGuardChanges) {
  generate(); controller.setManualText("Buffered meeting note");
  NotesEditorDialog dialog;
  auto *page = new CallNotesPage(db, &controller, &dialog);
  DuckClient client; client.name = "Synthetic"; page->setClient(client);
  dialog.setPage(page); dialog.resize(1100, 800); dialog.show(); QTest::qWait(80);
  auto *quick = page->findChild<QQuickWidget *>("notesQuickWorkspace");
  ASSERT_EQ(quick->status(), QQuickWidget::Ready);
  EXPECT_EQ(quick->rootObject()->objectName(), "qmlNotesEditorDialog");
  EXPECT_EQ(quick->rootObject()->findChild<QQuickItem *>("qmlMeetingHistory"), nullptr);
  auto *editor = quick->rootObject()->findChild<QQuickItem *>("qmlModalNoteEditor");
  ASSERT_NE(editor, nullptr);
  auto *document = editor->property("textDocument").value<QQuickTextDocument *>()->textDocument();
  QTextCursor cursor(document); cursor.movePosition(QTextCursor::End); cursor.insertText(" changed");
  EXPECT_TRUE(page->editorDirty());
  dialog.setDiscardConfirmation([] { return false; });
  dialog.reject(); EXPECT_TRUE(dialog.isVisible());
  dialog.close(); EXPECT_TRUE(dialog.isVisible());
  if (qEnvironmentVariableIsSet("SESSIO_MODAL_SCREENSHOT")) {
    QTest::qWait(80); dialog.grab().save(qEnvironmentVariable("SESSIO_MODAL_SCREENSHOT"));
    dialog.resize(640, 700); QTest::qWait(80);
    dialog.grab().save(qEnvironmentVariable("SESSIO_MODAL_SCREENSHOT") + "-narrow.png");
  }
  dialog.setDiscardConfirmation([] { return true; });
  dialog.reject(); EXPECT_FALSE(dialog.isVisible());
  EXPECT_EQ(controller.manualText(), "Buffered meeting note");
}

TEST_F(CallNotesPageTest, ModalSaveButtonPersistsAndAcceptsTheDialog) {
  controller.beginCall(); controller.setManualText("Save this draft");
  NotesEditorDialog dialog;
  auto *page = new CallNotesPage(db, &controller, &dialog);
  dialog.setPage(page); dialog.show(); QTest::qWait(30);
  auto *quick = page->findChild<QQuickWidget *>("notesQuickWorkspace");
  ASSERT_EQ(quick->status(), QQuickWidget::Ready);
  auto *save = quick->rootObject()->findChild<QQuickItem *>("qmlModalSaveNote");
  ASSERT_NE(save, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(save, "clicked"));
  EXPECT_EQ(dialog.result(), QDialog::Accepted);
  ASSERT_EQ(controller.noteEntries().size(), 1);
  EXPECT_EQ(controller.noteEntries().first().toObject()["body_markdown"].toString(), "Save this draft");
}

TEST_F(CallNotesPageTest, ModalFailureKeepsTextAndTheDialogOpen) {
  controller.beginCall();
  NotesEditorDialog dialog; auto *page = new CallNotesPage(db, &controller, &dialog);
  dialog.setPage(page); dialog.show(); QTest::qWait(30);
  auto *quick = page->findChild<QQuickWidget *>("notesQuickWorkspace");
  ASSERT_EQ(quick->status(), QQuickWidget::Ready);
  auto *editor = quick->rootObject()->findChild<QQuickItem *>("qmlModalNoteEditor");
  auto *document = editor->property("textDocument").value<QQuickTextDocument *>()->textDocument();
  QTextCursor cursor(document); cursor.insertText(QString(100001, 'x'));
  auto *save = quick->rootObject()->findChild<QQuickItem *>("qmlModalSaveNote");
  ASSERT_TRUE(QMetaObject::invokeMethod(save, "clicked"));
  EXPECT_TRUE(dialog.isVisible()); EXPECT_TRUE(page->editorDirty());
  EXPECT_EQ(document->toPlainText().size(), 100001);
  EXPECT_TRUE(controller.noteEntries().isEmpty());
  EXPECT_FALSE(page->findChild<NotesWorkspaceBridge *>()->saveStatus().isEmpty());
  dialog.setDiscardConfirmation([] { return true; }); dialog.reject();
}

TEST_F(CallNotesPageTest, ModalSavedEntryConflictsWithAnIndependentStorageWriter) {
  controller.beginCall(); const auto id = controller.saveNoteEntry("Original");
  NotesWorkspaceBridge bridge(db, &controller, nullptr); bridge.beginEditorSession();
  bridge.format(0, 8, "bold");
  CallNotesController other(db); other.openMaterial(controller.materialId());
  ASSERT_EQ(other.saveNoteEntry("External update", {}, id), id);
  EXPECT_FALSE(bridge.saveEditorSession()); EXPECT_TRUE(bridge.editorDirty());
  EXPECT_EQ(controller.noteEntries().first().toObject()["body_markdown"].toString(), "External update");
}

TEST_F(CallNotesPageTest, ModalSelectorShowsAllSavedNotesAndGuardsADirtySelection) {
  controller.beginCall();
  controller.saveNoteEntry("First saved note");
  const auto latest = controller.saveNoteEntry("Latest saved note");
  NotesEditorDialog dialog; auto *page = new CallNotesPage(db, &controller, &dialog);
  dialog.setPage(page); dialog.show(); QTest::qWait(30);
  auto *quick = page->findChild<QQuickWidget *>("notesQuickWorkspace");
  ASSERT_EQ(quick->status(), QQuickWidget::Ready);
  auto *selector = quick->rootObject()->findChild<QQuickItem *>("qmlEditorNoteSelector");
  ASSERT_NE(selector, nullptr);
  EXPECT_EQ(selector->property("count").toInt(), 3);
  EXPECT_EQ(selector->property("currentIndex").toInt(), 2);
  auto *bridge = page->findChild<NotesWorkspaceBridge *>();
  EXPECT_EQ(bridge->editorNoteId(), latest);
  bridge->format(0, 6, "bold");
  selector->setProperty("currentIndex", 0);
  ASSERT_TRUE(QMetaObject::invokeMethod(selector, "activated", Q_ARG(int, 0)));
  EXPECT_EQ(selector->property("currentIndex").toInt(), 2);
  EXPECT_EQ(bridge->editorNoteId(), latest);
  EXPECT_TRUE(bridge->editorDirty());
  dialog.setDiscardConfirmation([] { return true; }); dialog.reject();
}

TEST_F(CallNotesPageTest, QmlEditorFormatsAndSavesMarkdownThroughTheRealController) {
  controller.beginCall(); controller.setManualText(QString::fromUtf8("Привет мир"));
  CallNotesPage page(db, &controller); page.resize(1280, 860); page.show(); QApplication::processEvents();
  auto *quick = page.findChild<QQuickWidget *>("notesQuickWorkspace");
  ASSERT_NE(quick, nullptr);
  ASSERT_EQ(quick->status(), QQuickWidget::Ready);
  auto *editor = quick->rootObject()->findChild<QQuickItem *>("qmlNoteEditor");
  ASSERT_NE(editor, nullptr);
  auto *quickDocument = editor->property("textDocument").value<QQuickTextDocument *>();
  ASSERT_NE(quickDocument, nullptr);
  auto *bridge = page.findChild<NotesWorkspaceBridge *>(); ASSERT_NE(bridge, nullptr);
  bridge->format(0, 6, "bold");
  EXPECT_TRUE(controller.manualText().contains(QString::fromUtf8("**Привет**")));
  ASSERT_TRUE(bridge->saveNote());
  ASSERT_EQ(controller.noteEntries().size(), 1);
  EXPECT_TRUE(controller.noteEntries().first().toObject()["body_markdown"].toString().contains(QString::fromUtf8("**Привет**")));
  EXPECT_TRUE(quickDocument->textDocument()->toPlainText().isEmpty());
}

TEST_F(CallNotesPageTest, QmlEntryEditSurvivesAiUpdatesAndCancelRestoresDraftUndo) {
  controller.beginCall();
  const auto saved = controller.saveNoteEntry("Saved note");
  controller.setManualText("New draft");
  CallNotesPage page(db, &controller); page.show(); QApplication::processEvents();
  auto *quick = page.findChild<QQuickWidget *>("notesQuickWorkspace");
  ASSERT_EQ(quick->status(), QQuickWidget::Ready);
  auto *editor = quick->rootObject()->findChild<QQuickItem *>("qmlNoteEditor");
  auto *document = editor->property("textDocument").value<QQuickTextDocument *>();
  auto *draft = document->textDocument();
  QTextCursor cursor(draft); cursor.movePosition(QTextCursor::End); cursor.insertText(" plus");
  auto *bridge = page.findChild<NotesWorkspaceBridge *>();
  bridge->editNote(saved);
  QTextCursor edit(document->textDocument()); edit.movePosition(QTextCursor::End); edit.insertText(" buffered");
  controller.setManualText(controller.manualText() + "\n\nAI addition");
  EXPECT_EQ(document->textDocument()->toPlainText(), "Saved note buffered");
  bridge->cancelEdit();
  EXPECT_EQ(document->textDocument(), draft);
  EXPECT_TRUE(draft->toPlainText().contains("New draft plus"));
  EXPECT_TRUE(draft->isUndoAvailable());
}

TEST_F(CallNotesPageTest, QmlFailedSaveRetainsTheDocumentAndShowsFailure) {
  controller.beginCall();
  CallNotesPage page(db, &controller); page.show(); QApplication::processEvents();
  auto *quick = page.findChild<QQuickWidget *>("notesQuickWorkspace");
  ASSERT_EQ(quick->status(), QQuickWidget::Ready);
  auto *editor = quick->rootObject()->findChild<QQuickItem *>("qmlNoteEditor");
  auto *document = editor->property("textDocument").value<QQuickTextDocument *>();
  QTextCursor cursor(document->textDocument()); cursor.insertText(QString(100001, 'x'));
  auto *bridge = page.findChild<NotesWorkspaceBridge *>();
  EXPECT_FALSE(bridge->saveNote());
  EXPECT_EQ(document->textDocument()->toPlainText().size(), 100001);
  EXPECT_FALSE(bridge->saveStatus().isEmpty());
  EXPECT_TRUE(controller.noteEntries().isEmpty());
}

TEST_F(CallNotesPageTest, QmlSavingAnEntryPreservesTheFailedNewDraftAndItsUndo) {
  controller.beginCall();
  const auto saved = controller.saveNoteEntry("Existing note");
  CallNotesPage page(db, &controller); page.show(); QApplication::processEvents();
  auto *quick = page.findChild<QQuickWidget *>("notesQuickWorkspace"); ASSERT_EQ(quick->status(), QQuickWidget::Ready);
  auto *editor = quick->rootObject()->findChild<QQuickItem *>("qmlNoteEditor");
  auto *document = editor->property("textDocument").value<QQuickTextDocument *>();
  auto *draft = document->textDocument();
  QTextCursor cursor(draft); cursor.insertText(QString(100001, 'x'));
  auto *bridge = page.findChild<NotesWorkspaceBridge *>();
  bridge->editNote(saved);
  QTextCursor edit(document->textDocument()); edit.movePosition(QTextCursor::End); edit.insertText(" edited");
  ASSERT_TRUE(bridge->saveNote());
  EXPECT_EQ(document->textDocument(), draft);
  EXPECT_EQ(draft->toPlainText().size(), 100001);
  EXPECT_TRUE(draft->isUndoAvailable());
}

TEST_F(CallNotesPageTest, QmlOpeningAnotherSavedEntryKeepsTheUnfinishedEdit) {
  controller.beginCall();
  const auto first = controller.saveNoteEntry("First");
  const auto second = controller.saveNoteEntry("Second");
  CallNotesPage page(db, &controller); page.show(); QApplication::processEvents();
  auto *quick = page.findChild<QQuickWidget *>("notesQuickWorkspace"); ASSERT_EQ(quick->status(), QQuickWidget::Ready);
  auto *bridge = page.findChild<NotesWorkspaceBridge *>();
  auto *editor = quick->rootObject()->findChild<QQuickItem *>("qmlNoteEditor");
  auto *document = editor->property("textDocument").value<QQuickTextDocument *>();
  bridge->editNote(first);
  QTextCursor cursor(document->textDocument()); cursor.movePosition(QTextCursor::End); cursor.insertText(" buffered");
  bridge->editNote(second);
  EXPECT_EQ(document->textDocument()->toPlainText(), "First buffered");
  EXPECT_FALSE(bridge->saveStatus().isEmpty());
  EXPECT_TRUE(bridge->saveNote());
  EXPECT_EQ(controller.noteEntries().first().toObject()["body_markdown"].toString().trimmed(), "First buffered");
}

TEST_F(CallNotesPageTest, QmlKeyboardInputPersistsAndUndoReturnsToTheEmptyDraft) {
  controller.beginCall();
  CallNotesPage page(db, &controller); page.resize(1000, 750); page.show(); QApplication::processEvents();
  auto *quick = page.findChild<QQuickWidget *>("notesQuickWorkspace"); ASSERT_EQ(quick->status(), QQuickWidget::Ready);
  auto *editor = quick->rootObject()->findChild<QQuickItem *>("qmlNoteEditor"); ASSERT_NE(editor, nullptr);
  quick->setFocus(); editor->forceActiveFocus();
  QTest::keyClicks(quick, "Keyboard draft");
  EXPECT_TRUE(controller.manualText().contains("Keyboard draft"));
  QTest::keyClick(quick, Qt::Key_Z, Qt::ControlModifier);
  auto *document = editor->property("textDocument").value<QQuickTextDocument *>();
  EXPECT_TRUE(document->textDocument()->toPlainText().isEmpty());
  EXPECT_TRUE(controller.manualText().trimmed().isEmpty());
}

TEST_F(CallNotesPageTest, QmlSourceNavigationClearsSearchBeforeRevealingThePhrase) {
  generate();
  CallNotesPage page(db, &controller); page.resize(1280, 860); page.show(); QApplication::processEvents();
  auto *quick = page.findChild<QQuickWidget *>("notesQuickWorkspace"); ASSERT_EQ(quick->status(), QQuickWidget::Ready);
  auto *search = quick->rootObject()->findChild<QQuickItem *>("qmlPhraseSearch"); ASSERT_NE(search, nullptr);
  search->setProperty("text", "no matching phrase");
  ASSERT_TRUE(QMetaObject::invokeMethod(quick->rootObject(), "revealSource", Q_ARG(QVariant, QString::number(phrase.id))));
  QApplication::processEvents();
  EXPECT_TRUE(search->property("text").toString().isEmpty());
  EXPECT_TRUE(quick->rootObject()->property("sourceOpen").toBool());
  EXPECT_EQ(quick->rootObject()->property("highlightedSource").toString(), QString::number(phrase.id));
}

TEST_F(CallNotesPageTest, QmlHistoryDraftAndUndoStayBoundToTheOriginalMaterial) {
  DuckClient client; client.name = "Synthetic"; client.id = db->add_client(client);
  const auto transcript = db->add_transcript(std::nullopt, "live_local_v1");
  ASSERT_TRUE(db->set_transcript_status(transcript, "draft"));
  ASSERT_TRUE(db->set_transcript_clients(transcript, {client.id}));
  controller.beginCall(); controller.attachTranscript(transcript);
  const auto original = controller.materialId();
  CallNotesPage page(db, &controller); page.setClient(client); page.show(); QApplication::processEvents();
  auto *quick = page.findChild<QQuickWidget *>("notesQuickWorkspace"); ASSERT_EQ(quick->status(), QQuickWidget::Ready);
  auto *editor = quick->rootObject()->findChild<QQuickItem *>("qmlNoteEditor");
  auto *document = editor->property("textDocument").value<QQuickTextDocument *>();
  auto *firstDocument = document->textDocument();
  QTextCursor cursor(firstDocument); cursor.insertText("Original material draft");
  controller.finishCall(); controller.beginCall();
  const auto current = controller.materialId(); ASSERT_NE(original, current);
  auto *bridge = page.findChild<NotesWorkspaceBridge *>();
  ASSERT_FALSE(bridge->history().isEmpty());
  bridge->selectMaterial(QString::number(original));
  ASSERT_EQ(bridge->state()["materialId"].toString(), QString::number(original));
  EXPECT_EQ(document->textDocument(), firstDocument);
  EXPECT_TRUE(firstDocument->isUndoAvailable());
  EXPECT_TRUE(bridge->saveNote());
  EXPECT_EQ(controller.materialId(), current);
  EXPECT_TRUE(controller.noteEntries().isEmpty());
  CallNotesController stored(db); stored.openMaterial(original);
  ASSERT_EQ(stored.noteEntries().size(), 1);
  EXPECT_TRUE(stored.noteEntries().first().toObject()["body_markdown"].toString().contains("Original material draft"));
}

TEST_F(CallNotesPageTest, EmptyWidgetsAreHidden) {
  controller.beginCall();
  CallNotesPage page(db, &controller);
  page.show();
  EXPECT_FALSE(page.findChild<QWidget *>("meetingInfoCard")->isHidden());
  EXPECT_FALSE(page.findChild<QWidget *>("myNotesCard")->isHidden());
  EXPECT_TRUE(page.findChild<AiSummaryWidget *>()->isHidden());
  EXPECT_TRUE(page.findChild<KeyPointsWidget *>()->isHidden());
  EXPECT_TRUE(page.findChild<AgreementsWidget *>()->isHidden());
  EXPECT_TRUE(page.findChild<QWidget *>("notesModeSwitch")->isHidden());
  EXPECT_TRUE(page.findChild<PreviousMeetingWidget *>()->isHidden());
}

TEST_F(CallNotesPageTest, GeneratedSummaryAndAgreementsAppear) {
  CallNotesPage page(db, &controller);
  page.show();
  generate();
  EXPECT_FALSE(page.findChild<AiSummaryWidget *>()->isHidden());
  EXPECT_FALSE(page.findChild<KeyPointsWidget *>()->isHidden());
  page.setAgreements(QJsonArray{QJsonObject{{"text", "Sleep diary"}, {"active", true}}});
  EXPECT_FALSE(page.findChild<AgreementsWidget *>()->isHidden());
}

TEST_F(CallNotesPageTest, ClientShowsSwitchAndHistoryModeExpandsMeeting) {
  DuckClient client; client.name = std::string{"Test"}; client.last_name = std::string{"Client"};
  client.id = db->add_client(client);
  ASSERT_GT(client.id, 0);
  const auto past = db->create_call_material(std::nullopt);
  const auto transcript = db->add_transcript(std::nullopt, "live_local_v1");
  ASSERT_TRUE(db->link_call_material_transcript(past, transcript));
  ASSERT_TRUE(db->set_transcript_status(transcript, "draft"));
  ASSERT_TRUE(db->set_transcript_clients(transcript, {client.id}));
  ASSERT_TRUE(db->save_call_material(past, db->get_call_material(past).revision,
      R"({"ended":true,"final":{"note":{"text":"Past summary"}}})"));
  CallNotesPage page(db, &controller);
  page.setControllerFactory([this](qint64 id, QObject *parent) {
    auto *c = new CallNotesController(db, parent); c->openMaterial(id); return c;
  });
  page.setClient(client); page.show();
  EXPECT_FALSE(page.findChild<QWidget *>("notesModeSwitch")->isHidden());
  auto *previous = page.findChild<PreviousMeetingWidget *>();
  EXPECT_FALSE(previous->isHidden());
  previous->findChild<QPushButton *>("previousMeetingOpen")->click();
  EXPECT_TRUE(page.historyMode());
  EXPECT_FALSE(page.findChild<QWidget *>(QStringLiteral("historyBody_%1").arg(past))->isHidden());
  page.findChild<QPushButton *>("notesModeCurrent")->click();
  EXPECT_FALSE(page.historyMode());
}

TEST_F(CallNotesPageTest, EditorIsEmbedded) {
  CallNotesPage page(db, &controller);
  EXPECT_EQ(page.editor()->findChild<QScrollArea *>()->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
}

TEST_F(CallNotesPageTest, ManyPastCallsScrollInsideHistoryWithoutStretchingTheDocument) {
  DuckClient client; client.name = "Synthetic"; client.id = db->add_client(client);
  for (int i = 0; i < 24; ++i) {
    const auto material = db->create_call_material(std::nullopt);
    const auto transcript = db->add_transcript(std::nullopt, "live_local_v1");
    db->link_call_material_transcript(material, transcript); db->set_transcript_status(transcript, "draft");
    db->set_transcript_clients(transcript, {client.id});
    db->save_call_material(material, db->get_call_material(material).revision,
        R"({"ended":true,"final":{"note":{"text":"Review the observations and plan the next meeting."}}})");
  }
  generate();
  CallNotesPage page(db, &controller); page.setClient(client);
  page.setMeeting("Consultation · 60 minutes", "ended");
  page.resize(1280, 860); page.show(); QApplication::processEvents();
  auto *quick = page.findChild<QQuickWidget *>("notesQuickWorkspace");
  ASSERT_EQ(quick->status(), QQuickWidget::Ready);
  auto *history = quick->rootObject()->findChild<QQuickItem *>("qmlMeetingHistory");
  ASSERT_NE(history, nullptr);
  EXPECT_EQ(history->property("count").toInt(), 24);
  EXPECT_GT(history->property("contentHeight").toDouble(), history->height());
  EXPECT_LE(page.height(), 860);
  auto *editor = quick->rootObject()->findChild<QQuickItem *>("qmlNoteEditor");
  ASSERT_NE(editor, nullptr);
  EXPECT_LT(editor->mapToScene(QPointF{}).x(), 350);
  page.resize(2360, 1400); QTest::qWait(80);
  auto *paper = quick->rootObject()->findChild<QQuickItem *>("qmlNotesPaper");
  ASSERT_NE(paper, nullptr);
  const auto center = paper->mapToScene(QPointF(paper->width() / 2, 0)).x();
  EXPECT_NEAR(center, (320 + quick->width()) / 2.0, 3);
  EXPECT_GT(paper->width(), quick->width() - 400);
  EXPECT_GT(paper->height(), quick->height() - 240);
  EXPECT_EQ(quick->rootObject()->property("paper").value<QColor>(), QApplication::palette().color(QPalette::Base));
  EXPECT_EQ(quick->rootObject()->property("accent").value<QColor>(), QApplication::palette().color(QPalette::Highlight));
  EXPECT_GE(editor->property("font").value<QFont>().pixelSize(), 17);
  if (qEnvironmentVariableIsSet("SESSIO_CALL_NOTES_SCREENSHOT")) {
    QTest::qWait(80);
    page.grab().save(qEnvironmentVariable("SESSIO_CALL_NOTES_SCREENSHOT"));
    quick->rootObject()->setProperty("sourceOpen", true); QTest::qWait(80);
    page.grab().save(qEnvironmentVariable("SESSIO_CALL_NOTES_SCREENSHOT") + "-sources.png");
    quick->rootObject()->setProperty("sourceOpen", false);
    page.resize(680, 860); QTest::qWait(80);
    page.grab().save(qEnvironmentVariable("SESSIO_CALL_NOTES_SCREENSHOT") + "-narrow.png");
  }
}

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  // QApplication owns and deletes the style; the static marks that it is not leaked.
  static oclero::qlementine::QlementineStyle *style = nullptr;
  style = new oclero::qlementine::QlementineStyle(&app);
  if (qEnvironmentVariableIsSet("SESSIO_PREVIEW_THEME"))
    style->setThemeJsonPath(qEnvironmentVariable("SESSIO_PREVIEW_THEME"));
  app.setStyle(style);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
