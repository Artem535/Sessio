#include "call_notes_panel.h"
#include "call_notes_controller.h"
#include "call_side_panel.h"
#include "transcript_panel.h"
#include "prompter_panel.h"
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>
#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <gtest/gtest.h>

class CallNotesPanelTest : public ::testing::Test {
protected:
  QTemporaryDir dir;
  pcm::config::Config conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir.path().toStdString())}};
  std::shared_ptr<pcm::database::Database> db = std::make_shared<pcm::database::Database>(conf);
  CallNotesController controller{db};
  CallNotesPanel panel{&controller};
  DuckTranscriptPhrase phrase;

  void generate(const QString &text = QStringLiteral("Prepare a short outline.")) {
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
    reply["notes"] = QJsonObject{{"note", note}, {"key_points", QJsonArray{note}}};
    controller.handleResult(reply);
    ASSERT_EQ(controller.draft()["text"].toString(), text);
  }
};

TEST_F(CallNotesPanelTest, EmptyPageDoesNotCreateMaterialsOrInventDrafts) {
  EXPECT_EQ(controller.materialId(), 0);
  EXPECT_TRUE(panel.findChild<QPlainTextEdit *>("callNotesManual")->toPlainText().isEmpty());
  EXPECT_TRUE(panel.findChild<QLabel *>("callNotesDraftText")->text().isEmpty());
  EXPECT_TRUE(panel.findChildren<QPushButton *>("callNotesSource").isEmpty());
  QSignalSpy start(&panel, &CallNotesPanel::startRequested);
  panel.findChild<QPushButton *>("callNotesStart")->click();
  EXPECT_EQ(start.count(), 1);
  EXPECT_EQ(controller.materialId(), 0);
}

TEST_F(CallNotesPanelTest, ManualTypingSurvivesControllerUpdatesWithoutMovingCursorOrFocus) {
  generate();
  panel.show();
  auto *manual = panel.findChild<QPlainTextEdit *>("callNotesManual");
  manual->setFocus(); QTest::keyClicks(manual, "My own notes");
  auto cursor = manual->textCursor(); cursor.setPosition(3); manual->setTextCursor(cursor);
  controller.changed(); QCoreApplication::processEvents();
  EXPECT_EQ(manual->toPlainText(), "My own notes");
  EXPECT_EQ(controller.manualText(), "My own notes");
  EXPECT_EQ(manual->textCursor().position(), 3);
  EXPECT_EQ(panel.focusWidget(), manual);
}

TEST_F(CallNotesPanelTest, DraftEditorSurvivesStatusUpdates) {
  generate(); panel.show();
  panel.findChild<QPushButton *>("callNotesEdit")->click();
  auto *editor = panel.findChild<QPlainTextEdit *>("callNotesDraftEditor");
  editor->selectAll(); QTest::keyClicks(editor, "My revised draft");
  controller.changed();
  EXPECT_EQ(editor->toPlainText(), "My revised draft");
  panel.findChild<QPushButton *>("callNotesEdit")->click();
  EXPECT_EQ(controller.draft()["text"].toString(), "My revised draft");
}

TEST_F(CallNotesPanelTest, SourceClickRequestsTheActualPhrase) {
  generate();
  QSignalSpy sources(&panel, &CallNotesPanel::sourceRequested);
  const auto links = panel.findChildren<QPushButton *>("callNotesSource");
  ASSERT_FALSE(links.isEmpty()); links.first()->click();
  ASSERT_EQ(sources.count(), 1);
  EXPECT_EQ(sources.first().first().toLongLong(), phrase.id);
}

TEST_F(CallNotesPanelTest, AcceptPreservesManualNotesAndCannotAppendTheSameDraftTwice) {
  generate();
  auto *manual = panel.findChild<QPlainTextEdit *>("callNotesManual");
  manual->setPlainText("My own notes");
  QSignalSpy saved(&panel, &CallNotesPanel::noteSaved);
  auto *accept = panel.findChild<QPushButton *>("callNotesAccept");
  accept->click(); accept->click();
  EXPECT_EQ(controller.manualText(), "My own notes\n\nPrepare a short outline.");
  EXPECT_EQ(manual->toPlainText(), controller.manualText());
  EXPECT_EQ(saved.count(), 1);
  EXPECT_FALSE(accept->isEnabled());
  CallNotesController reopened(db); reopened.openMaterial(controller.materialId());
  CallNotesPanel savedPanel(&reopened);
  EXPECT_FALSE(savedPanel.findChild<QPushButton *>("callNotesAccept")->isEnabled());
}

TEST_F(CallNotesPanelTest, NewGenerationKeepsTheEditingSnapshotUntilEditingIsCancelled) {
  generate(); panel.show();
  panel.findChild<QPushButton *>("callNotesEdit")->click();
  auto *editor = panel.findChild<QPlainTextEdit *>("callNotesDraftEditor");
  editor->selectAll(); QTest::keyClicks(editor, "Keep my revision");
  QSignalSpy requests(&controller, &CallNotesController::requestReady);
  auto next = phrase; next.text = "We also discussed rehearsal."; next.start_ms = 78000; next.end_ms = 80000;
  next.id = db->add_transcript_phrase(next); controller.addPhrase(next); controller.flush();
  ASSERT_EQ(requests.count(), 1);
  auto reply = requests.first().first().toJsonObject();
  const QJsonObject note{{"text", "New model proposal"}, {"evidence_ids", QJsonArray{QString::number(next.id)}}};
  reply["notes"] = QJsonObject{{"note", note}, {"key_points", QJsonArray{note}}};
  controller.handleResult(reply);
  EXPECT_EQ(editor->toPlainText(), "Keep my revision");
  EXPECT_EQ(panel.findChild<QLabel *>("callNotesPoint")->text(), "• Prepare a short outline.");
  EXPECT_FALSE(panel.findChild<QPushButton *>("callNotesAccept")->isEnabled());
  EXPECT_FALSE(panel.findChild<QPushButton *>("callNotesEdit")->isEnabled());
  for (auto *source : panel.findChildren<QPushButton *>("callNotesSource")) EXPECT_FALSE(source->isEnabled());
  panel.findChild<QPushButton *>("callNotesDismiss")->click();
  EXPECT_EQ(panel.findChild<QLabel *>("callNotesDraftText")->text(), "New model proposal");
  EXPECT_EQ(controller.draft()["text"].toString(), "New model proposal");
}

TEST_F(CallNotesPanelTest, FinalRequestUsesControllerAndCancelRetainsTheLiveDraft) {
  generate(); controller.finishCall();
  QObject::connect(&controller, &CallNotesController::generationRequested, &controller,
                   [this] { controller.setGenerationEnabled(true); });
  QSignalSpy requested(&controller, &CallNotesController::generationRequested);
  controller.cancel();
  panel.findChild<QPushButton *>("callNotesFinal")->click();
  EXPECT_EQ(requested.count(), 1);
  EXPECT_EQ(controller.liveStatus(), "ready");
  EXPECT_EQ(controller.finalStatus(), "running");
  EXPECT_FALSE(panel.findChild<QLabel *>("callNotesFinalStatus")->isHidden());
  panel.findChild<QPushButton *>("callNotesCancel")->click();
  EXPECT_EQ(controller.finalStatus(), "cancelled");
  EXPECT_EQ(panel.findChild<QLabel *>("callNotesDraftText")->text(), "Prepare a short outline.");
  EXPECT_EQ(controller.liveStatus(), "ready");
}

TEST_F(CallNotesPanelTest, ChangedEvidenceDisablesAcceptanceAndSourceNavigation) {
  generate();
  ASSERT_TRUE(db->update_transcript_phrase_text(phrase.id, "Corrected source"));
  controller.changed();
  EXPECT_FALSE(panel.findChild<QPushButton *>("callNotesAccept")->isEnabled());
  for (auto *source : panel.findChildren<QPushButton *>("callNotesSource")) EXPECT_FALSE(source->isEnabled());
  EXPECT_EQ(panel.findChild<QLabel *>("callNotesDraftText")->text(), "Prepare a short outline.");
}

TEST_F(CallNotesPanelTest, SavedMeetingWithoutASRLoadsAndPersistsManualNotes) {
  controller.beginCall();
  panel.findChild<QPlainTextEdit *>("callNotesManual")->setPlainText("Notes without transcription");
  const auto id = controller.materialId();
  CallNotesController reopened(db); reopened.openMaterial(id);
  CallNotesPanel savedPanel(&reopened);
  auto *manual = savedPanel.findChild<QPlainTextEdit *>("callNotesManual");
  EXPECT_TRUE(manual->isEnabled());
  EXPECT_EQ(manual->toPlainText(), "Notes without transcription");
  EXPECT_EQ(reopened.transcriptId(), 0);
  manual->setPlainText("Updated meeting notes");
  CallNotesController check(db); check.openMaterial(id);
  EXPECT_EQ(check.manualText(), "Updated meeting notes");
  EXPECT_TRUE(savedPanel.findChild<QLabel *>("callNotesDraftText")->text().isEmpty());
}

TEST_F(CallNotesPanelTest, NativeTabsRenderWithTheApplicationTheme) {
  const auto path = qEnvironmentVariable("SESSIO_NOTES_SCREENSHOT_DIR");
  if (path.isEmpty()) GTEST_SKIP() << "Screenshot output not requested";
  generate(QStringLiteral("Обсудили подготовку к выступлению. Участник планирует начать с короткой репетиции и подготовить план."));
  ASSERT_TRUE(controller.setManualText(QStringLiteral("На следующей встрече обсудить опыт репетиции.")));
  auto *notes = new CallNotesPanel(&controller);
  auto *transcript = new TranscriptPanel;
  auto sample = phrase; sample.text = "Начну с короткой репетиции, а потом подготовлю план."; sample.speaker_name = "Участник";
  transcript->addPhrase(sample);
  CallSidePanel side(notes, transcript);
  CallPrompterController agreements(db); agreements.beginTranscript(phrase.transcript_id);
  auto *prompter = new PrompterPanel(&agreements, db);
  side.setPrompterPanel(prompter, prompter->sharedHintWidget());
  side.resize(440, 820); side.show(); QCoreApplication::processEvents();
  auto *style = dynamic_cast<oclero::qlementine::QlementineStyle *>(QApplication::style());
  ASSERT_NE(style, nullptr); QDir().mkpath(path);
  for (const auto &theme : {QStringLiteral("light"), QStringLiteral("dark")}) {
    style->setThemeJsonPath(QStringLiteral(SESSIO_SOURCE_DIR) + "/resources/themes/qlementine_" + theme + ".json");
    side.showNotes(); QTest::qWait(400);
    ASSERT_TRUE(side.grab().save(QDir(path).filePath("native-notes-" + theme + ".png")));
    side.showTranscript(); QTest::qWait(400);
    ASSERT_TRUE(side.grab().save(QDir(path).filePath("native-transcript-" + theme + ".png")));
    side.showPrompter(); QTest::qWait(400);
    ASSERT_TRUE(side.grab().save(QDir(path).filePath("native-prompter-" + theme + ".png")));
  }
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setStyle(new oclero::qlementine::QlementineStyle(&app));
  QTranslator ru;
  if (!qEnvironmentVariable("SESSIO_NOTES_SCREENSHOT_DIR").isEmpty() &&
      ru.load(QCoreApplication::applicationDirPath() + "/../app_ru.qm")) app.installTranslator(&ru);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
