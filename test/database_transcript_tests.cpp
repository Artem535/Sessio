#include <Poco/File.h>
#include <Poco/Path.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "config.h"
#include "database.h"

namespace {

// One temporary database directory per test, removed before and after.
class TranscriptDbTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
    dir_ = Poco::Path(Poco::Path::current())
               .append(std::string("tmp_transcript_") + info->name())
               .toString();
    removeDir();
    pcm::config::Config conf{
        .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir_)}};
    db_ = std::make_unique<pcm::database::Database>(conf);
  }

  void TearDown() override {
    db_.reset();
    removeDir();
  }

  void removeDir() {
    Poco::File dir(dir_);
    if (dir.exists()) dir.remove(true);
  }

  int64_t makeEvent(int64_t start_ms = 1730000000000) {
    DuckEvent event;
    event.name = std::string{"Session"};
    event.start_date = start_ms;
    event.end_date = start_ms + 3600000;
    event.duration = 3600;
    event.event_stat_id = 1;
    event.payment_stat_id = 1;
    return db_->add_event(event);
  }

  std::string dir_;
  std::unique_ptr<pcm::database::Database> db_;
};

}  // namespace

TEST_F(TranscriptDbTest, AddTranscriptStartsRecordingWithConsentTimes) {
  const auto event_id = makeEvent();
  ASSERT_GT(event_id, 0);
  const auto id = db_->add_transcript(event_id, "live_local_v1", std::string{"gigaam-v3-rnnt"},
                                      1730000100000);
  ASSERT_GT(id, 0);

  const auto t = db_->get_transcript(id);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->id, id);
  EXPECT_EQ(t->event_id, event_id);
  EXPECT_EQ(t->status, "recording");
  EXPECT_EQ(t->consent_scope, "live_local_v1");
  EXPECT_EQ(t->consent_given_at, 1730000100000);
  EXPECT_FALSE(t->consent_revoked_at.has_value());
  EXPECT_EQ(t->model_id.value_or(""), "gigaam-v3-rnnt");
  EXPECT_GT(t->created_at, 0);
  EXPECT_EQ(t->updated_at, t->created_at);
}

TEST_F(TranscriptDbTest, AddTranscriptRejectsInvalidAndUnknownEvent) {
  EXPECT_EQ(db_->add_transcript(0, "live_local_v1"), 0);
  EXPECT_EQ(db_->add_transcript(-5, "live_local_v1"), 0);
  EXPECT_EQ(db_->add_transcript(987654, "live_local_v1"), 0);  // no such event
}

TEST_F(TranscriptDbTest, ConsentGivenAtDefaultsToNow) {
  const auto id = db_->add_transcript(makeEvent(), "live_local_v1");
  ASSERT_GT(id, 0);
  const auto t = db_->get_transcript(id);
  ASSERT_NE(t, nullptr);
  EXPECT_GT(t->consent_given_at, 1700000000000);  // some time after 2023
  EXPECT_FALSE(t->model_id.has_value());
}

TEST_F(TranscriptDbTest, GetTranscriptsForEventReturnsOnlyThatEventInCreationOrder) {
  const auto a = makeEvent(1730000000000);
  const auto b = makeEvent(1740000000000);
  const auto a1 = db_->add_transcript(a, "live_local_v1");
  const auto b1 = db_->add_transcript(b, "live_local_v1");
  const auto a2 = db_->add_transcript(a, "live_local_v1");

  const auto for_a = db_->get_transcripts_for_event(a);
  ASSERT_EQ(for_a.size(), 2u);
  EXPECT_EQ(for_a[0].id, a1);
  EXPECT_EQ(for_a[1].id, a2);
  const auto for_b = db_->get_transcripts_for_event(b);
  ASSERT_EQ(for_b.size(), 1u);
  EXPECT_EQ(for_b[0].id, b1);
  EXPECT_TRUE(db_->get_transcripts_for_event(0).empty());
}

TEST_F(TranscriptDbTest, GetTranscriptUnknownIdIsNull) {
  EXPECT_EQ(db_->get_transcript(12345), nullptr);
  EXPECT_EQ(db_->get_transcript(0), nullptr);
}

TEST_F(TranscriptDbTest, SetStatusValidatesAndUpdates) {
  const auto id = db_->add_transcript(makeEvent(), "live_local_v1");
  ASSERT_TRUE(db_->set_transcript_status(id, "draft"));
  EXPECT_EQ(db_->get_transcript(id)->status, "draft");
  ASSERT_TRUE(db_->set_transcript_status(id, "reviewed"));
  EXPECT_EQ(db_->get_transcript(id)->status, "reviewed");

  EXPECT_FALSE(db_->set_transcript_status(id, "published"));
  EXPECT_EQ(db_->get_transcript(id)->status, "reviewed");
  EXPECT_FALSE(db_->set_transcript_status(777, "draft"));
}

TEST_F(TranscriptDbTest, RevokeConsentRecordsTimeAndKeepsRow) {
  const auto id = db_->add_transcript(makeEvent(), "live_local_v1");
  ASSERT_TRUE(db_->revoke_transcript_consent(id, 1730000200000));
  const auto t = db_->get_transcript(id);
  ASSERT_NE(t, nullptr);
  ASSERT_TRUE(t->consent_revoked_at.has_value());
  EXPECT_EQ(*t->consent_revoked_at, 1730000200000);
  EXPECT_FALSE(db_->revoke_transcript_consent(4242));
}

TEST_F(TranscriptDbTest, FinalizeInterruptedTurnsRecordingIntoDraftOnly) {
  const auto event_id = makeEvent();
  const auto recording = db_->add_transcript(event_id, "live_local_v1");
  const auto reviewed = db_->add_transcript(event_id, "live_local_v1");
  ASSERT_TRUE(db_->set_transcript_status(reviewed, "reviewed"));
  const auto draft = db_->add_transcript(event_id, "live_local_v1");
  ASSERT_TRUE(db_->set_transcript_status(draft, "draft"));

  EXPECT_EQ(db_->finalize_interrupted_transcripts(), 1);
  EXPECT_EQ(db_->get_transcript(recording)->status, "draft");
  EXPECT_EQ(db_->get_transcript(reviewed)->status, "reviewed");
  EXPECT_EQ(db_->get_transcript(draft)->status, "draft");
  EXPECT_EQ(db_->finalize_interrupted_transcripts(), 0);
}

TEST_F(TranscriptDbTest, DeleteTranscriptAndDeleteAll) {
  const auto event_id = makeEvent();
  const auto one = db_->add_transcript(event_id, "live_local_v1");
  const auto two = db_->add_transcript(event_id, "live_local_v1");
  ASSERT_TRUE(db_->delete_transcript(one));
  EXPECT_EQ(db_->get_transcript(one), nullptr);
  EXPECT_NE(db_->get_transcript(two), nullptr);
  EXPECT_FALSE(db_->delete_transcript(one));  // already gone

  ASSERT_TRUE(db_->delete_all_transcripts());
  EXPECT_TRUE(db_->get_transcripts_for_event(event_id).empty());
  EXPECT_TRUE(db_->delete_all_transcripts());  // nothing to delete is not an error
}
