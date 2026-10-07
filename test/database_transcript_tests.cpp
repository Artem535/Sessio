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

namespace {

DuckTranscriptPhrase phrase(int64_t transcript_id, const char *role, const char *speaker,
                            int64_t start_ms, int64_t end_ms, const char *text) {
  DuckTranscriptPhrase p;
  p.transcript_id = transcript_id;
  p.track_role = role;
  p.speaker_name = std::string{speaker};
  p.start_ms = start_ms;
  p.end_ms = end_ms;
  p.text = text;
  return p;
}

}  // namespace

TEST_F(TranscriptDbTest, PhrasesRoundTripAndAreOrderedByStartTime) {
  const auto t = db_->add_transcript(makeEvent(), "live_local_v1");
  const auto late = db_->add_transcript_phrase(phrase(t, "participant", "Client", 5000, 7000, "second"));
  const auto early = db_->add_transcript_phrase(phrase(t, "practitioner", "Specialist", 1000, 3000, "first"));
  ASSERT_GT(late, 0);
  ASSERT_GT(early, 0);

  const auto phrases = db_->get_transcript_phrases(t);
  ASSERT_EQ(phrases.size(), 2u);
  EXPECT_EQ(phrases[0].id, early);
  EXPECT_EQ(phrases[0].transcript_id, t);
  EXPECT_EQ(phrases[0].track_role, "practitioner");
  EXPECT_EQ(phrases[0].speaker_name.value_or(""), "Specialist");
  EXPECT_EQ(phrases[0].start_ms, 1000);
  EXPECT_EQ(phrases[0].end_ms, 3000);
  EXPECT_EQ(phrases[0].text, "first");
  EXPECT_FALSE(phrases[0].edited);
  EXPECT_EQ(phrases[1].id, late);
}

TEST_F(TranscriptDbTest, PhraseWithoutSpeakerNameAndCyrillicTextRoundTrips) {
  const auto t = db_->add_transcript(makeEvent(), "live_local_v1");
  DuckTranscriptPhrase p = phrase(t, "participant", "x", 0, 900, "Привет, как дела?");
  p.speaker_name = std::nullopt;
  ASSERT_GT(db_->add_transcript_phrase(p), 0);
  const auto phrases = db_->get_transcript_phrases(t);
  ASSERT_EQ(phrases.size(), 1u);
  EXPECT_FALSE(phrases[0].speaker_name.has_value());
  EXPECT_EQ(phrases[0].text, "Привет, как дела?");
}

TEST_F(TranscriptDbTest, AddPhraseRejectsInvalidInput) {
  const auto t = db_->add_transcript(makeEvent(), "live_local_v1");
  EXPECT_EQ(db_->add_transcript_phrase(phrase(0, "participant", "C", 0, 1, "x")), 0);
  EXPECT_EQ(db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1, "")), 0);
  EXPECT_EQ(db_->add_transcript_phrase(phrase(t, "participant", "C", 500, 100, "x")), 0);
  EXPECT_EQ(db_->add_transcript_phrase(phrase(424242, "participant", "C", 0, 1, "x")), 0);
  EXPECT_TRUE(db_->get_transcript_phrases(t).empty());
}

TEST_F(TranscriptDbTest, UpdatePhraseTextMarksEditedAndBumpsTranscript) {
  const auto t = db_->add_transcript(makeEvent(), "live_local_v1");
  const auto id = db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "mispelled"));
  const auto before = db_->get_transcript(t)->updated_at;

  ASSERT_TRUE(db_->update_transcript_phrase_text(id, "misspelled"));
  const auto phrases = db_->get_transcript_phrases(t);
  ASSERT_EQ(phrases.size(), 1u);
  EXPECT_EQ(phrases[0].text, "misspelled");
  EXPECT_TRUE(phrases[0].edited);
  EXPECT_GE(db_->get_transcript(t)->updated_at, before);

  EXPECT_FALSE(db_->update_transcript_phrase_text(id, ""));
  EXPECT_FALSE(db_->update_transcript_phrase_text(31337, "x"));
  EXPECT_EQ(db_->get_transcript_phrases(t)[0].text, "misspelled");
}

TEST_F(TranscriptDbTest, DeletePhraseRemovesOnlyThatPhrase) {
  const auto t = db_->add_transcript(makeEvent(), "live_local_v1");
  const auto a = db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "a"));
  const auto b = db_->add_transcript_phrase(phrase(t, "participant", "C", 2000, 3000, "b"));
  ASSERT_TRUE(db_->delete_transcript_phrase(a));
  const auto phrases = db_->get_transcript_phrases(t);
  ASSERT_EQ(phrases.size(), 1u);
  EXPECT_EQ(phrases[0].id, b);
  EXPECT_FALSE(db_->delete_transcript_phrase(a));
}

TEST_F(TranscriptDbTest, StatusUpdateWorksOnTranscriptThatHasPhrases) {
  const auto t = db_->add_transcript(makeEvent(), "live_local_v1");
  ASSERT_GT(db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "a")), 0);
  ASSERT_TRUE(db_->set_transcript_status(t, "draft"));
  ASSERT_TRUE(db_->revoke_transcript_consent(t));
  EXPECT_EQ(db_->get_transcript(t)->status, "draft");
  EXPECT_EQ(db_->get_transcript_phrases(t).size(), 1u);
}

TEST_F(TranscriptDbTest, EditingAnEventKeepsItsTranscript) {
  const auto event_id = makeEvent();
  const auto t = db_->add_transcript(event_id, "live_local_v1");
  ASSERT_GT(db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "a")), 0);

  auto event = db_->get_event(event_id);
  ASSERT_NE(event, nullptr);
  event->name = std::string{"Renamed"};
  event->event_stat_id = 2;
  ASSERT_TRUE(db_->update_event(*event));
  EXPECT_NE(db_->get_transcript(t), nullptr);
  EXPECT_EQ(db_->get_transcript_phrases(t).size(), 1u);
}

TEST_F(TranscriptDbTest, DeleteTranscriptRemovesItsPhrases) {
  const auto event_id = makeEvent();
  const auto keep = db_->add_transcript(event_id, "live_local_v1");
  const auto drop = db_->add_transcript(event_id, "live_local_v1");
  ASSERT_GT(db_->add_transcript_phrase(phrase(keep, "participant", "C", 0, 1000, "keep")), 0);
  ASSERT_GT(db_->add_transcript_phrase(phrase(drop, "participant", "C", 0, 1000, "drop")), 0);

  ASSERT_TRUE(db_->delete_transcript(drop));
  EXPECT_TRUE(db_->get_transcript_phrases(drop).empty());
  EXPECT_EQ(db_->get_transcript_phrases(keep).size(), 1u);

  ASSERT_TRUE(db_->delete_all_transcripts());
  EXPECT_TRUE(db_->get_transcript_phrases(keep).empty());
  EXPECT_EQ(db_->get_transcript(keep), nullptr);
}
