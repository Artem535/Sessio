#include "notes_widget_frame.h"
#include "ai_summary_widget.h"
#include "call_notes_controller.h"
#include "key_points_widget.h"
#include "agreements_widget.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <gtest/gtest.h>

TEST(NotesWidgetFrameTest, ShowsTitleStatusChipAndLabelledActions) {
  NotesWidgetFrame frame(QStringLiteral("AI summary"));
  EXPECT_EQ(frame.findChild<QLabel *>("notesWidgetTitle")->text(), "AI summary");
  auto *chip = frame.findChild<QLabel *>("notesWidgetStatus");
  EXPECT_TRUE(chip->isHidden());
  frame.setStatus(QStringLiteral("ready"));
  EXPECT_FALSE(chip->isHidden()); EXPECT_EQ(chip->text(), "ready");
  EXPECT_EQ(frame.findChild<QToolButton *>("notesWidgetMore"), nullptr);
  auto *edit = frame.addAction(QStringLiteral("Edit"), "aiSummaryEdit");
  EXPECT_EQ(edit->text(), "Edit");
  auto *remove = frame.addMenuAction(QStringLiteral("Delete"), "aiSummaryDelete");
  auto *more = frame.findChild<QToolButton *>("notesWidgetMore");
  ASSERT_NE(more, nullptr);
  EXPECT_FALSE(more->accessibleName().isEmpty());
  EXPECT_TRUE(more->menu()->actions().contains(remove));
  frame.setStatus({}); EXPECT_TRUE(chip->isHidden());
}

// Produces a controller with one ready summary (made after the call) and one key point.
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
    controller.addPhrase(phrase); controller.finishCall(); controller.flush();
    ASSERT_FALSE(requests.isEmpty());
    ASSERT_TRUE(requests.last().first().toJsonObject()["final"].toBool());
    auto reply = requests.last().first().toJsonObject();
    const QJsonObject note{{"text", text}, {"evidence_ids", QJsonArray{QString::number(phrase.id)}}};
    auto point = note; point["text"] = pointText;
    reply["notes"] = QJsonObject{{"note", note}, {"key_points", QJsonArray{point}}};
    controller.handleResult(reply);
    ASSERT_EQ(controller.draft()["text"].toString(), text);
  }
};

class AiSummaryTest : public ::testing::Test, public NotesFixture {};

TEST_F(AiSummaryTest, ShowsSummaryWithoutAcceptOrEditorButtons) {
  generate();
  AiSummaryWidget widget(&controller);
  EXPECT_TRUE(widget.hasContent());
  EXPECT_EQ(widget.findChild<QLabel *>("aiSummaryText")->text(), "Prepare a short outline.");
  for (auto *button : widget.findChildren<QPushButton *>()) {
    EXPECT_FALSE(button->text().isEmpty()) << "every button is labelled";
    EXPECT_FALSE(button->text().contains("editor", Qt::CaseInsensitive));
  }
  EXPECT_EQ(widget.findChild<QLabel *>("notesWidgetStatus")->text(), "ready");
}

TEST_F(AiSummaryTest, EditSavesTextInPlace) {
  generate();
  AiSummaryWidget widget(&controller);
  widget.show();
  widget.findChild<QPushButton *>("aiSummaryEdit")->click();
  auto *editor = widget.findChild<QPlainTextEdit *>("aiSummaryEditor");
  ASSERT_FALSE(editor->isHidden());
  editor->setPlainText("Revised summary");
  widget.findChild<QPushButton *>("aiSummarySave")->click();
  EXPECT_EQ(controller.draft()["text"].toString(), "Revised summary");
  EXPECT_TRUE(editor->isHidden());
}

TEST_F(AiSummaryTest, DeletingSummaryAsksForConfirmation) {
  generate();
  AiSummaryWidget widget(&controller);
  int asked = 0; bool answer = false;
  widget.setDeleteConfirmation([&](QWidget *) { ++asked; return answer; });
  auto *remove = widget.findChild<QAction *>("aiSummaryDelete");
  remove->trigger();
  EXPECT_EQ(asked, 1); EXPECT_FALSE(controller.draft().isEmpty());
  answer = true; remove->trigger();
  EXPECT_TRUE(controller.draft().isEmpty());
  EXPECT_FALSE(widget.hasContent());
}

TEST_F(AiSummaryTest, AiSummaryErrorShowsReasonAndRetryInsideCard) {
  generate();
  QSignalSpy requests(&controller, &CallNotesController::requestReady);
  controller.requestFinal(); // "Create again" after the call
  ASSERT_FALSE(requests.isEmpty());
  auto failed = requests.last().first().toJsonObject();
  failed["error"] = "x"; failed["reason"] = "too_long";
  controller.handleResult(failed);
  AiSummaryWidget widget(&controller);
  widget.show();
  auto *problem = widget.findChild<QLabel *>("aiSummaryProblem");
  EXPECT_FALSE(problem->isHidden());
  EXPECT_TRUE(problem->text().contains("too long"));
  const auto before = requests.count();
  controller.setGenerationEnabled(true);
  widget.findChild<QPushButton *>("aiSummaryRetry")->click();
  EXPECT_EQ(requests.count(), before + 1);
  EXPECT_TRUE(requests.last().first().toJsonObject()["final"].toBool());
}

TEST_F(AiSummaryTest, SourceLinkRequestsThePhrase) {
  generate();
  AiSummaryWidget widget(&controller);
  QSignalSpy sources(&widget, &AiSummaryWidget::sourceRequested);
  auto links = widget.findChildren<QPushButton *>("callNotesSource");
  ASSERT_FALSE(links.isEmpty()); links.first()->click();
  ASSERT_EQ(sources.count(), 1);
  EXPECT_EQ(sources.first().first().toLongLong(), phrase.id);
}

TEST_F(AiSummaryTest, SourceLinkDisablesWhenTranscriptChanges) {
  generate();
  AiSummaryWidget widget(&controller);
  auto *link = widget.findChild<QPushButton *>("callNotesSource");
  ASSERT_NE(link, nullptr); EXPECT_TRUE(link->isEnabled());
  db->update_transcript_phrase_text(phrase.id, "Corrected");
  emit controller.changed();
  link = widget.findChild<QPushButton *>("callNotesSource");
  ASSERT_NE(link, nullptr); EXPECT_FALSE(link->isEnabled());
}

TEST_F(AiSummaryTest, EmptyIdleControllerHasNoContent) {
  AiSummaryWidget widget(&controller);
  EXPECT_FALSE(widget.hasContent());
}

class KeyPointsTest : public ::testing::Test, public NotesFixture {};

TEST_F(KeyPointsTest, ListsPointsWithCompactSource) {
  generate();
  KeyPointsWidget widget(&controller);
  ASSERT_TRUE(widget.hasContent());
  const auto rows = widget.findChildren<QWidget *>("keyPointRow");
  ASSERT_EQ(rows.size(), 1);
  EXPECT_EQ(rows[0]->findChild<QLabel *>("keyPointText")->text(), "Outline is due");
  EXPECT_EQ(rows[0]->findChild<QPushButton *>("callNotesSource")->text(), "1:13");
  EXPECT_EQ(widget.findChild<QLabel *>("notesWidgetStatus")->text(), "1");
}

TEST_F(KeyPointsTest, KeyPointMatchingSummaryTextIsHidden) {
  generate(QStringLiteral("Same text."), QStringLiteral("Same text."));
  KeyPointsWidget widget(&controller);
  EXPECT_TRUE(widget.findChildren<QWidget *>("keyPointRow").isEmpty());
  EXPECT_FALSE(widget.hasContent());
}

TEST(AgreementsWidgetTest, ShowsActiveAgreementsOnly) {
  AgreementsWidget widget;
  EXPECT_FALSE(widget.hasContent());
  widget.setRecords(QJsonArray{
      QJsonObject{{"id", "a"}, {"text", "Keep a sleep diary"}, {"active", true}},
      QJsonObject{{"id", "b"}, {"text", "Cancelled call"}, {"active", false}}});
  const auto rows = widget.findChildren<QLabel *>("agreementRow");
  ASSERT_EQ(rows.size(), 1);
  EXPECT_EQ(rows[0]->text(), "Keep a sleep diary");
  EXPECT_TRUE(widget.hasContent());
}

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  // QApplication owns and deletes the style; the static marks that it is not leaked.
  static oclero::qlementine::QlementineStyle *style = nullptr;
  style = new oclero::qlementine::QlementineStyle(&app);
  app.setStyle(style);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
