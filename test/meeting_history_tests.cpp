#include <QTest>
#include <QFrame>
#include <QTextBrowser>
#include "client_notes_page.h"
#include "meeting_history.h"
#include "meeting_history_widgets.h"
#include "call_notes_controller.h"
#include "ai_summary_widget.h"
#include <QDateTime>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalSpy>
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

TEST_F(MeetingHistoryTest, ClientNotesFeedShowsFinalAiSummaries) {
  meeting({{"final", QJsonObject{{"note", QJsonObject{{"text", "Whole meeting summary"}}}}}});
  meeting({{"live", QJsonObject{{"note", QJsonObject{{"text", "Live draft only"}}}}}});
  meeting({{"final", QJsonObject{{"dismissed", true}, {"note", QJsonObject{{"text", "Deleted"}}}}}});
  meeting({{"final", QJsonObject{{"note", QJsonObject{{"text", "Call still running"}}}}}}, false);
  ClientNotesPage page(db);
  page.setClientInfo(*db->get_client(client));
  auto *journal = page.findChild<QListWidget *>("notesJournalList");
  ASSERT_NE(journal, nullptr);
  QStringList summaries;
  for (int i = 0; i < journal->count(); ++i) {
    journal->setCurrentRow(i);
    for (const auto *bubble : page.findChildren<QFrame *>("aiSummaryBubble")) {
      if (bubble->isHidden()) continue;
      const auto *body = bubble->findChild<QTextBrowser *>("notesEntryBody");
      ASSERT_NE(body, nullptr);
      summaries.append(body->toPlainText().trimmed());
    }
  }
  EXPECT_EQ(summaries, QStringList{QStringLiteral("Whole meeting summary")});
}

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
  EXPECT_EQ(list.findChildren<QPushButton *>(QRegularExpression("^historyRow_")).size(), 2);
  EXPECT_EQ(list.findChild<QLabel *>("historyLooseNote")->text(), "Loose");
  list.expand(first);
  auto *firstBody = list.findChild<QWidget *>(QStringLiteral("historyBody_%1").arg(first));
  ASSERT_NE(firstBody, nullptr); EXPECT_FALSE(firstBody->isHidden());
  EXPECT_EQ(firstBody->findChild<QLabel *>("aiSummaryText")->text(), "First");
  list.expand(second);
  EXPECT_TRUE(firstBody->isHidden());
  EXPECT_EQ(created, 2);
}

TEST_F(MeetingHistoryTest, CardShowsCountersAndSwapsPreviewForBodyWhenExpanded) {
  const auto id = meeting({{"final", QJsonObject{{"note", QJsonObject{{"text", "Sum"}}}}},
                           {"manual_entries", QJsonArray{QJsonObject{{"body_markdown", "Own note"}}}}});
  MeetingHistoryList list;
  list.setControllerFactory([&](qint64 mid, QObject *parent) {
    auto *c = new CallNotesController(db, parent); c->openMaterial(mid); return c;
  });
  list.setMeetings(loadMeetingHistory(*db, client, 0), {});
  list.show();
  auto *toggle = list.findChild<QPushButton *>(QStringLiteral("historyRow_%1").arg(id));
  ASSERT_NE(toggle, nullptr);
  auto *card = qobject_cast<NotesWidgetFrame *>(toggle->parentWidget()->parentWidget());
  ASSERT_NE(card, nullptr);
  bool chip = false;
  for (auto *l : card->findChildren<QLabel *>())
    if (l->text().startsWith("AI summary") && l->text().contains("1")) chip = true;
  EXPECT_TRUE(chip);
  auto *preview = list.findChild<QLabel *>(QStringLiteral("historyPreview_%1").arg(id));
  ASSERT_NE(preview, nullptr);
  EXPECT_TRUE(preview->isVisible());
  toggle->click();
  EXPECT_FALSE(preview->isVisible());
  auto *note = list.findChild<QLabel *>("historyNote");
  ASSERT_NE(note, nullptr);
  EXPECT_EQ(note->text(), "Own note");
  toggle->click();
  EXPECT_TRUE(preview->isVisible());
}

TEST_F(MeetingHistoryTest, MissingEndedKeyCountsAsEnded) {
  const auto id = db->create_call_material(std::nullopt);
  const auto transcript = db->add_transcript(std::nullopt, "live_local_v1");
  ASSERT_TRUE(db->link_call_material_transcript(id, transcript));
  ASSERT_TRUE(db->set_transcript_status(transcript, "draft"));
  ASSERT_TRUE(db->set_transcript_clients(transcript, {client}));
  const auto revision = db->get_call_material(id).revision;
  ASSERT_TRUE(db->save_call_material(id, revision, "{}"));
  EXPECT_EQ(loadMeetingHistory(*db, client, 0).size(), 1u);
}

TEST_F(MeetingHistoryTest, MaterialAgreementsCollectTranscriptRecords) {
  const auto id = meeting({});
  EXPECT_TRUE(loadMaterialAgreements(*db, id).isEmpty());
  const auto transcript = db->get_call_material_transcript_ids(id).front();
  const QJsonObject state{{"records", QJsonArray{QJsonObject{{"text", "Walk daily"}, {"active", true}}}}};
  ASSERT_TRUE(db->save_prompter_state(transcript, db->get_prompter_state(transcript).revision, QJsonDocument(state).toJson(QJsonDocument::Compact).toStdString()));
  const auto records = loadMaterialAgreements(*db, id);
  ASSERT_EQ(records.size(), 1);
  EXPECT_EQ(records[0].toObject()["text"].toString(), "Walk daily");
}

TEST_F(MeetingHistoryTest, ExpandedCardHidesEmptySummary) {
  const auto id = meeting({});
  MeetingHistoryList list;
  list.setControllerFactory([&](qint64 mid, QObject *parent) {
    auto *c = new CallNotesController(db, parent); c->openMaterial(mid); return c;
  });
  list.setMeetings(loadMeetingHistory(*db, client, 0), {});
  list.show();
  list.expand(id);
  auto *summary = list.findChild<AiSummaryWidget *>();
  ASSERT_NE(summary, nullptr);
  EXPECT_FALSE(summary->isVisible());
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
