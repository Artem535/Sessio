#include "meeting_history.h"
#include "meeting_history_widgets.h"
#include "call_notes_controller.h"
#include <QDateTime>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QToolButton>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <gtest/gtest.h>

class MeetingHistoryTest : public ::testing::Test {
protected:
  QTemporaryDir dir;
  pcm::config::Config conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir.path().toStdString())}};
  std::shared_ptr<pcm::database::Database> db = std::make_shared<pcm::database::Database>(conf);
  int64_t client = 0;
  void SetUp() override {
    DuckClient c; c.name = std::string{"Test"}; c.last_name = std::string{"Client"};
    client = db->add_client(c);
    ASSERT_GT(client, 0);
  }
  // Ended material linked to the client through a transcript.
  qint64 meeting(const QJsonObject &state, bool ended = true) {
    const auto id = db->create_call_material(std::nullopt);
    const auto transcript = db->add_transcript(std::nullopt, "live_local_v1");
    EXPECT_TRUE(db->link_call_material_transcript(id, transcript));
    EXPECT_TRUE(db->set_transcript_status(transcript, "draft"));
    EXPECT_TRUE(db->set_transcript_clients(transcript, {client}));
    auto json = state; json["ended"] = ended;
    const auto revision = db->get_call_material(id).revision;
    EXPECT_TRUE(db->save_call_material(id, revision, QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString()));
    return id;
  }
};

TEST_F(MeetingHistoryTest, HistoryListsClientMeetingsNewestFirst) {
  const auto older = meeting({{"final", QJsonObject{{"note", QJsonObject{{"text", "Older summary"}}}}}});
  const auto newer = meeting({{"live", QJsonObject{{"note", QJsonObject{{"text", "Newer summary"}}}}},
                              {"manual_entries", QJsonArray{QJsonObject{{"body_markdown", "My note"}}}}});
  meeting({}, false);  // the current, unfinished call is not history
  const auto history = loadMeetingHistory(*db, client, 0);
  ASSERT_EQ(history.size(), 2u);
  EXPECT_EQ(history[0].materialId, newer);
  EXPECT_EQ(history[0].summary, "Newer summary");
  EXPECT_EQ(history[0].noteCount, 1);
  EXPECT_EQ(history[0].firstNote, "My note");
  EXPECT_EQ(history[1].materialId, older);
  EXPECT_EQ(loadMeetingHistory(*db, client, newer).size(), 1u);
}

TEST_F(MeetingHistoryTest, DismissedSummaryFallsBackToFirstNoteInPreview) {
  meeting({{"final", QJsonObject{{"dismissed", true}, {"note", QJsonObject{{"text", "Gone"}}}}},
           {"manual_entries", QJsonArray{QJsonObject{{"body_markdown", "Line one\nLine two\nLine three"}}}}});
  const auto history = loadMeetingHistory(*db, client, 0);
  ASSERT_EQ(history.size(), 1u);
  EXPECT_TRUE(history[0].summary.isEmpty());
  EXPECT_EQ(meetingPreview(history[0]), "Line one\nLine two");
}

TEST_F(MeetingHistoryTest, HistoryGroupsNotesWithoutMeetingSeparately) {
  DuckClientNote loose; loose.client_id = client; loose.body_markdown = std::string{"Loose note"};
  ASSERT_GT(db->add_client_note(loose), 0);
  const auto notes = loadNotesWithoutMeeting(*db, client);
  ASSERT_EQ(notes.size(), 1);
  EXPECT_EQ(notes[0], "Loose note");
}

TEST(PreviousMeetingWidgetTest, PreviousMeetingShowsLatestCompletedMeetingOfClient) {
  PreviousMeetingWidget widget;
  EXPECT_TRUE(widget.isHidden());
  MeetingRecord record;
  record.materialId = 7; record.startMs = QDateTime(QDate(2026, 10, 14), QTime(15, 30)).toMSecsSinceEpoch();
  record.durationMs = 50 * 60000; record.summary = "Anxiety before meetings";
  record.agreements = {"Breathing practice", "Sleep diary", "Walk", "Fourth"};
  widget.setMeeting(record);
  EXPECT_FALSE(widget.isHidden());
  EXPECT_TRUE(widget.findChild<QLabel *>("previousMeetingDate")->text().startsWith("14.10"));
  EXPECT_EQ(widget.findChild<QLabel *>("previousMeetingPreview")->text(), "Anxiety before meetings");
  EXPECT_EQ(widget.findChildren<QLabel *>("previousMeetingAgreement").size(), 3);
  QSignalSpy open(&widget, &PreviousMeetingWidget::openRequested);
  widget.findChild<QPushButton *>("previousMeetingOpen")->click();
  ASSERT_EQ(open.count(), 1); EXPECT_EQ(open[0][0].toLongLong(), 7);
  widget.setMeeting(std::nullopt); EXPECT_TRUE(widget.isHidden());
}

TEST_F(MeetingHistoryTest, ExpandingAMeetingShowsItsWidgetsAndCollapsesOthers) {
  const auto first = meeting({{"final", QJsonObject{{"note", QJsonObject{{"text", "First"}}}}}});
  const auto second = meeting({{"final", QJsonObject{{"note", QJsonObject{{"text", "Second"}}}}}});
  MeetingHistoryList list;
  int created = 0;
  list.setControllerFactory([&](qint64 id, QObject *parent) {
    ++created; auto *c = new CallNotesController(db, parent); c->openMaterial(id); return c;
  });
  list.setMeetings(loadMeetingHistory(*db, client, 0), {"Loose"});
  EXPECT_EQ(list.findChildren<QToolButton *>(QRegularExpression("^historyRow_")).size(), 2);
  EXPECT_EQ(list.findChild<QLabel *>("historyLooseNote")->text(), "Loose");
  list.expand(first);
  auto *firstBody = list.findChild<QWidget *>(QStringLiteral("historyBody_%1").arg(first));
  ASSERT_NE(firstBody, nullptr); EXPECT_FALSE(firstBody->isHidden());
  EXPECT_EQ(firstBody->findChild<QLabel *>("aiSummaryText")->text(), "First");
  list.expand(second);
  EXPECT_TRUE(firstBody->isHidden());
  EXPECT_EQ(created, 2);
}

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  app.setStyle(new oclero::qlementine::QlementineStyle(&app));
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
