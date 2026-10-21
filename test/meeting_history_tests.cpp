#include "meeting_history.h"
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

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  app.setStyle(new oclero::qlementine::QlementineStyle(&app));
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
