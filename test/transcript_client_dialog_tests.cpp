#include "transcript_client_dialog.h"
#include "config.h"
#include <QApplication>
#include <QListWidget>
#include <QTemporaryDir>
#include <gtest/gtest.h>

TEST(TranscriptClientTest, ExistingIdsArePreselectedAndGuestNamesDoNotCreateClients) {
  QTemporaryDir temp;
  pcm::config::Config conf;
  conf.db_conf.value_.db_pth = Poco::Path(temp.path().toStdString());
  auto db = std::make_shared<pcm::database::Database>(conf);
  DuckClient client; client.name = "Existing";
  const auto clientId = db->add_client(client);
  const auto id = db->add_transcript(std::nullopt, "scope");
  ASSERT_TRUE(db->set_transcript_status(id, "draft"));
  ASSERT_TRUE(db->set_transcript_clients(id, {clientId}));
  const auto count = db->get_clients().size();
  TranscriptClientDialog dialog(db, id);
  auto *list = dialog.findChild<QListWidget *>("transcriptClientSelection");
  ASSERT_NE(list, nullptr);
  ASSERT_EQ(list->count(), count);
  EXPECT_EQ(dialog.selectedClientIds(), std::vector<int64_t>{clientId});
  for (int i = 0; i < list->count(); ++i) list->item(i)->setCheckState(Qt::Unchecked);
  EXPECT_TRUE(dialog.selectedClientIds().empty());
  EXPECT_EQ(db->get_clients().size(), count);
  EXPECT_EQ(db->get_transcript_client_ids(id), std::vector<int64_t>{clientId});
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
