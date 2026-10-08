#include "main_window.h"
#include "token_backend_client.h"

#include <QApplication>
#include <QPushButton>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTableWidget>
#ifdef SESSIO_CALL_TRANSCRIPTION
#include "transcript_page.h"
#include "transcript_list_page.h"
#endif
#include <gtest/gtest.h>

TEST(MainWindowCallsTabTest, AddCallsPageAddsCallsTabAndPage) {
  MainWindow window;
  pcm::video::DeviceManager deviceManager;
  pcm::tokenclient::TokenBackendClient tokenClient("http://127.0.0.1:1");

  window.addCallsPage(&deviceManager, &tokenClient, [] { return QString(); });

  EXPECT_NE(window.getPage(MainWindow::Pages::calls), nullptr);
  EXPECT_NE(window.findChild<QPushButton *>(), nullptr); // sidebar buttons exist
}

#ifdef SESSIO_CALL_TRANSCRIPTION
TEST(MainWindowCallsTabTest, StandaloneTranscriptListAndReviewNeedNoEventOrClient) {
  QTemporaryDir storage;
  pcm::config::Config config;
  config.db_conf.value_.db_pth = Poco::Path(storage.path().toStdString());
  auto db = std::make_shared<pcm::database::Database>(config);
  const auto id = db->add_transcript(std::nullopt, "live_local_v1");
  ASSERT_GT(id, 0);
  ASSERT_TRUE(db->set_transcript_status(id, "draft"));
  MainWindow window;
  auto *list = new TranscriptListPage(db);
  auto *review = new TranscriptPage(db, 0, {});
  window.registerTranscriptListPage(list);
  window.registerTranscriptPage(review);
  window.resize(1100, 750);
  window.show();
  window.openTranscriptListPage();
  QApplication::processEvents();
  EXPECT_EQ(window.findChild<QStackedWidget *>()->currentWidget(), list);
  auto *table = list->findChild<QTableWidget *>("transcriptList");
  ASSERT_NE(table, nullptr);
  ASSERT_EQ(table->rowCount(), 1);
  EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole).toLongLong(), id);
  review->reloadTranscript(id, "Transcript");
  window.openTranscriptPage();
  QApplication::processEvents();
  EXPECT_EQ(window.findChild<QStackedWidget *>()->currentWidget(), review);
  EXPECT_EQ(review->transcriptCount(), 1);
  EXPECT_TRUE(db->get_clients().empty());
  EXPECT_FALSE(db->get_transcript(id)->event_id.has_value());
  const auto screenshot = qEnvironmentVariable("SESSIO_REVIEW_SCREENSHOT");
  if (!screenshot.isEmpty()) EXPECT_TRUE(window.grab().save(screenshot));
}

TEST(MainWindowCallsTabTest, TranscriptReviewOpensInsideMainWindowWithPopulatedData) {
  QTemporaryDir storage;
  pcm::config::DatabaseConfig databaseConfig;
  databaseConfig.db_pth = Poco::Path(storage.path().toStdString());
  pcm::config::Config config;
  config.db_conf = databaseConfig;
  auto db = std::make_shared<pcm::database::Database>(config);
  DuckEvent event;
  event.name = "Test session";
  event.start_date = 1730000000000;
  event.end_date = *event.start_date + 3600000;
  event.duration = 3600;
  event.event_stat_id = 1;
  event.payment_stat_id = 1;
  const auto eventId = db->add_event(event);
  const auto transcript = db->add_transcript(eventId, "live_local_v1", std::string("gigaam-v3-rnnt"));
  DuckTranscriptPhrase phrase;
  phrase.transcript_id = transcript;
  phrase.track_role = "practitioner";
  phrase.start_ms = 1000;
  phrase.end_ms = 4000;
  phrase.text = "What would you like to discuss today?";
  ASSERT_GT(db->add_transcript_phrase(phrase), 0);
  ASSERT_TRUE(db->set_transcript_status(transcript, "draft"));
  MainWindow window;
  auto *page = new TranscriptPage(db, eventId, "Test session");
  window.registerTranscriptPage(page);
  window.resize(1100, 750);
  window.show();
  window.openTranscriptPage();
  QApplication::processEvents();
  EXPECT_EQ(window.getPage(MainWindow::Pages::transcript), page);
  EXPECT_EQ(window.findChild<QStackedWidget *>()->currentWidget(), page);
  EXPECT_FALSE(page->isWindow());
  EXPECT_EQ(page->transcriptCount(), 1);
  const auto screenshot = qEnvironmentVariable("SESSIO_REVIEW_SCREENSHOT");
  if (!screenshot.isEmpty()) EXPECT_TRUE(window.grab().save(screenshot));
}
#endif

int main(int argc, char **argv) {
  QTemporaryDir home;
  qputenv("HOME", home.path().toUtf8());
  qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
