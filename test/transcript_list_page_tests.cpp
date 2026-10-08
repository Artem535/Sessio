#include "transcript_list_page.h"
#include "transcript_page.h"
#include "transcript_client_dialog.h"
#include "config.h"
#include <QApplication>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QListWidget>
#include <QTimer>
#include <QTableWidget>
#include <QTemporaryDir>
#include <gtest/gtest.h>

class TranscriptListTest : public ::testing::Test {
protected:
  void SetUp() override {
    conf.db_conf.value_.db_pth = Poco::Path(temp.path().toStdString());
    db = std::make_shared<pcm::database::Database>(conf);
  }
  QTemporaryDir temp;
  pcm::config::Config conf;
  std::shared_ptr<pcm::database::Database> db;
};

TEST_F(TranscriptListTest, StandaloneSurvivesRestartOpenEditExportBindUnbindAndDelete) {
  DuckClient client; client.name = "Existing client";
  const auto clientId = db->add_client(client);
  const auto id = db->add_transcript(std::nullopt, "live_local_v1");
  DuckTranscriptPhrase phrase; phrase.transcript_id = id; phrase.track_role = "participant";
  phrase.text = "Before edit"; phrase.start_ms = 100; phrase.end_ms = 200;
  const auto phraseId = db->add_transcript_phrase(phrase);
  ASSERT_GT(phraseId, 0);
  ASSERT_TRUE(db->set_transcript_status(id, "draft"));
  db.reset(); db = std::make_shared<pcm::database::Database>(conf);
  TranscriptListPage list(db);
  auto *table = list.findChild<QTableWidget *>("transcriptList");
  ASSERT_EQ(table->rowCount(), 1);
  EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole).toLongLong(), id);
  QSignalSpy open(&list, &TranscriptListPage::openTranscriptRequested);
  table->selectRow(0);
  list.findChild<QPushButton *>("openTranscript")->click();
  ASSERT_EQ(open.count(), 1);
  TranscriptPage review(db, 0, {});
  review.reloadTranscript(open.at(0).at(0).toLongLong(), "Transcript");
  ASSERT_EQ(review.transcriptCount(), 1);
  review.findChild<QPushButton *>("editPhrase_" + QString::number(phraseId))->click();
  review.findChild<QPlainTextEdit *>("phraseEditor")->setPlainText("After edit");
  review.findChild<QPushButton *>("savePhrase")->click();
  EXPECT_TRUE(review.exportText().contains("After edit"));
  QTimer::singleShot(0, [&] {
    for (auto *widget : QApplication::topLevelWidgets())
      if (auto *dialog = qobject_cast<TranscriptClientDialog *>(widget)) {
        auto *clients = dialog->findChild<QListWidget *>("transcriptClientSelection");
        for (int i = 0; i < clients->count(); ++i)
          if (clients->item(i)->data(Qt::UserRole).toLongLong() == clientId)
            clients->item(i)->setCheckState(Qt::Checked);
        dialog->accept();
      }
  });
  review.findChild<QPushButton *>("transcriptAttachClients")->click();
  EXPECT_EQ(db->get_transcript_client_ids(id), std::vector<int64_t>{clientId});
  list.reload();
  EXPECT_EQ(table->item(0, 2)->text(), "Existing client");
  QTimer::singleShot(0, [&] {
    for (auto *widget : QApplication::topLevelWidgets())
      if (auto *dialog = qobject_cast<TranscriptClientDialog *>(widget)) {
        auto *clients = dialog->findChild<QListWidget *>("transcriptClientSelection");
        for (int i = 0; i < clients->count(); ++i) clients->item(i)->setCheckState(Qt::Unchecked);
        dialog->accept();
      }
  });
  review.findChild<QPushButton *>("transcriptAttachClients")->click();
  EXPECT_TRUE(db->get_transcript_client_ids(id).empty());
  list.reload();
  EXPECT_EQ(table->item(0, 2)->text(), "No clients attached");
  list.setConfirmHook([](const QString &) { return true; });
  table->selectRow(0);
  list.findChild<QPushButton *>("deleteTranscript")->click();
  EXPECT_EQ(table->rowCount(), 0);
  EXPECT_EQ(db->get_transcript(id), nullptr);
  EXPECT_TRUE(db->get_transcript_phrases(id).empty());
}

TEST_F(TranscriptListTest, RecordingAndRejectedDeleteKeepTheText) {
  const auto id = db->add_transcript(std::nullopt, "scope");
  TranscriptListPage list(db);
  auto *table = list.findChild<QTableWidget *>("transcriptList");
  list.setConfirmHook([](const QString &) { return true; });
  table->selectRow(0); list.findChild<QPushButton *>("deleteTranscript")->click();
  EXPECT_NE(db->get_transcript(id), nullptr);
  ASSERT_TRUE(db->set_transcript_status(id, "draft"));
  list.setConfirmHook([](const QString &) { return false; });
  list.findChild<QPushButton *>("deleteTranscript")->click();
  EXPECT_NE(db->get_transcript(id), nullptr);
}

TEST_F(TranscriptListTest, ReloadAnotherIdDoesNotLosePendingEdit) {
  const auto first = db->add_transcript(std::nullopt, "scope");
  const auto second = db->add_transcript(std::nullopt, "scope");
  DuckTranscriptPhrase phrase; phrase.transcript_id = first; phrase.text = "Original";
  phrase.track_role = "participant"; phrase.end_ms = 100;
  const auto phraseId = db->add_transcript_phrase(phrase);
  ASSERT_TRUE(db->set_transcript_status(first, "draft"));
  ASSERT_TRUE(db->set_transcript_status(second, "draft"));
  TranscriptPage review(db, 0, {}); review.reloadTranscript(first, "First");
  review.findChild<QPushButton *>("editPhrase_" + QString::number(phraseId))->click();
  review.findChild<QPlainTextEdit *>("phraseEditor")->setPlainText("Pending");
  review.reloadTranscript(second, "Second");
  ASSERT_TRUE(review.editing());
  EXPECT_EQ(review.findChild<QPlainTextEdit *>("phraseEditor")->toPlainText(), "Pending");
  review.findChild<QPushButton *>("savePhrase")->click();
  EXPECT_EQ(db->get_transcript_phrases(first).at(0).text, "Pending");
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
