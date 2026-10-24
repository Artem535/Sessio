#include "call_notes_page.h"
#include "agreements_widget.h"
#include "ai_summary_widget.h"
#include "call_notes_controller.h"
#include "client_notes_page.h"
#include "key_points_widget.h"
#include "meeting_history_widgets.h"
#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
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

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  app.setStyle(new oclero::qlementine::QlementineStyle(&app));
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
