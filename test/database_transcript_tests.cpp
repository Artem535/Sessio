#include <Poco/File.h>
#include <Poco/Path.h>
#include <duckdb.hpp>
#include <gtest/gtest.h>
#include <plog/Appenders/IAppender.h>
#include <plog/Log.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

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
  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  ASSERT_TRUE(db_->update_transcript_phrase_text(id, "misspelled"));
  const auto phrases = db_->get_transcript_phrases(t);
  ASSERT_EQ(phrases.size(), 1u);
  EXPECT_EQ(phrases[0].text, "misspelled");
  EXPECT_TRUE(phrases[0].edited);
  EXPECT_GT(db_->get_transcript(t)->updated_at, before);

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

TEST_F(TranscriptDbTest, RemoveEventDeletesItsTranscriptsAndPhrases) {
  const auto event_id = makeEvent();
  const auto other_id = makeEvent(1750000000000);
  const auto t = db_->add_transcript(event_id, "live_local_v1");
  const auto other = db_->add_transcript(other_id, "live_local_v1");
  ASSERT_GT(db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "gone")), 0);
  ASSERT_GT(db_->add_transcript_phrase(phrase(other, "participant", "C", 0, 1000, "stays")), 0);

  ASSERT_TRUE(db_->remove_event(event_id));
  EXPECT_EQ(db_->get_event(event_id), nullptr);
  EXPECT_EQ(db_->get_transcript(t), nullptr);
  EXPECT_TRUE(db_->get_transcript_phrases(t).empty());
  EXPECT_NE(db_->get_transcript(other), nullptr);
  EXPECT_EQ(db_->get_transcript_phrases(other).size(), 1u);
}

TEST_F(TranscriptDbTest, RemoveEventLeavesNoOrphanTranscriptsOfAnyStatus) {
  const auto event_id = makeEvent();
  const auto recording = db_->add_transcript(event_id, "live_local_v1");
  const auto draft = db_->add_transcript(event_id, "live_local_v1");
  const auto reviewed = db_->add_transcript(event_id, "live_local_v1");
  for (const auto id : {recording, draft, reviewed}) {
    ASSERT_GT(db_->add_transcript_phrase(phrase(id, "participant", "C", 0, 1000, "x")), 0);
  }
  ASSERT_TRUE(db_->set_transcript_status(draft, "draft"));
  ASSERT_TRUE(db_->set_transcript_status(reviewed, "reviewed"));

  ASSERT_TRUE(db_->remove_event(event_id));
  EXPECT_TRUE(db_->get_transcripts_for_event(event_id).empty());
  for (const auto id : {recording, draft, reviewed}) {
    EXPECT_EQ(db_->get_transcript(id), nullptr);
    EXPECT_TRUE(db_->get_transcript_phrases(id).empty());
  }
}

TEST_F(TranscriptDbTest, DeletingSeriesOverridesDeletesTheirTranscripts) {
  DuckEventSeries series;
  series.name = std::string{"Weekly"};
  series.start_date = 1730000000000;
  series.end_date = 1730003600000;
  series.duration = 3600;
  series.recurrence_rule = "FREQ=WEEKLY;INTERVAL=1";
  const auto series_id = db_->add_event_series(series);
  ASSERT_GT(series_id, 0);

  DuckEvent override_event;
  override_event.name = std::string{"Moved occurrence"};
  override_event.start_date = 1730100000000;
  override_event.end_date = 1730103600000;
  override_event.duration = 3600;
  override_event.event_stat_id = 1;
  override_event.payment_stat_id = 1;
  override_event.series_id = series_id;
  override_event.original_occurrence_start = 1730100000000;
  const auto event_id = db_->add_event(override_event);
  ASSERT_GT(event_id, 0);

  // An override of an occurrence BEFORE the cutoff must be kept.
  DuckEvent early_event = override_event;
  early_event.name = std::string{"Earlier occurrence"};
  early_event.start_date = 1729000000000;
  early_event.end_date = 1729003600000;
  early_event.original_occurrence_start = 1729000000000;
  const auto early_id = db_->add_event(early_event);
  ASSERT_GT(early_id, 0);
  const auto early_t = db_->add_transcript(early_id, "live_local_v1");
  ASSERT_GT(db_->add_transcript_phrase(phrase(early_t, "participant", "C", 0, 1000, "kept")), 0);

  const auto other_id = makeEvent(1750000000000);
  const auto other = db_->add_transcript(other_id, "live_local_v1");
  ASSERT_GT(db_->add_transcript_phrase(phrase(other, "participant", "C", 0, 1000, "stays")), 0);

  const auto t = db_->add_transcript(event_id, "live_local_v1");
  ASSERT_GT(db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "x")), 0);

  ASSERT_TRUE(db_->delete_event_series_overrides_from(series_id, 1730000000001));
  EXPECT_NE(db_->get_event(early_id), nullptr);
  EXPECT_NE(db_->get_transcript(early_t), nullptr);
  EXPECT_EQ(db_->get_transcript_phrases(early_t).size(), 1u);
  EXPECT_EQ(db_->get_event(event_id), nullptr);
  EXPECT_EQ(db_->get_transcript(t), nullptr);
  EXPECT_TRUE(db_->get_transcript_phrases(t).empty());
  EXPECT_NE(db_->get_transcript(other), nullptr);
  EXPECT_EQ(db_->get_transcript_phrases(other).size(), 1u);
}

TEST_F(TranscriptDbTest, OpeningADatabaseWithoutTranscriptTablesCreatesThem) {
  // Simulate a database created by an older version: remove the new tables.
  db_.reset();
  {
    duckdb::DuckDB raw(dir_ + "/database.db");
    duckdb::Connection conn(raw);
    ASSERT_FALSE(conn.Query("DROP TABLE IF EXISTS TranscriptPhrase")->HasError());
    ASSERT_FALSE(conn.Query("DROP TABLE IF EXISTS Transcript")->HasError());
  }

  pcm::config::Config conf{
      .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir_)}};
  db_ = std::make_unique<pcm::database::Database>(conf);

  const auto event_id = makeEvent();
  const auto t = db_->add_transcript(event_id, "live_local_v1");
  ASSERT_GT(t, 0);
  ASSERT_GT(db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "works")), 0);
  EXPECT_EQ(db_->get_transcript_phrases(t).size(), 1u);
}

TEST_F(TranscriptDbTest, ExistingDataSurvivesTheTranscriptMigration) {
  const auto event_id = makeEvent();
  db_.reset();
  {
    duckdb::DuckDB raw(dir_ + "/database.db");
    duckdb::Connection conn(raw);
    ASSERT_FALSE(conn.Query("DROP TABLE IF EXISTS TranscriptPhrase")->HasError());
    ASSERT_FALSE(conn.Query("DROP TABLE IF EXISTS Transcript")->HasError());
  }
  pcm::config::Config conf{
      .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir_)}};
  db_ = std::make_unique<pcm::database::Database>(conf);
  EXPECT_NE(db_->get_event(event_id), nullptr);
}

TEST_F(TranscriptDbTest, SchemaVersionStaysOne) {
  EXPECT_EQ(db_->get_application_metadata().schema_version, 1);
}

TEST_F(TranscriptDbTest, PurgeOrphanTranscriptsRemovesTheOnesWithoutAnEventAndFreedIdsAreClean) {
  const auto keep_event = makeEvent(1730000000000);
  const auto doomed_event = makeEvent(1740000000000);
  ASSERT_GT(doomed_event, keep_event);
  const auto keep = db_->add_transcript(keep_event, "live_local_v1");
  const auto orphan = db_->add_transcript(doomed_event, "live_local_v1");
  ASSERT_GT(db_->add_transcript_phrase(phrase(keep, "participant", "C", 0, 1000, "stays")), 0);
  ASSERT_GT(db_->add_transcript_phrase(phrase(orphan, "participant", "C", 0, 1000, "gone")), 0);

  // Simulate a missed cascade: delete the event row behind the Database's back.
  db_.reset();
  {
    duckdb::DuckDB raw(dir_ + "/database.db");
    duckdb::Connection conn(raw);
    ASSERT_FALSE(
        conn.Query("DELETE FROM Event WHERE id = " + std::to_string(doomed_event))->HasError());
  }
  pcm::config::Config conf{
      .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir_)}};
  db_ = std::make_unique<pcm::database::Database>(conf);

  EXPECT_EQ(db_->purge_orphan_transcripts(), 1);
  EXPECT_EQ(db_->get_transcript(orphan), nullptr);
  EXPECT_TRUE(db_->get_transcript_phrases(orphan).empty());
  EXPECT_NE(db_->get_transcript(keep), nullptr);
  EXPECT_EQ(db_->get_transcript_phrases(keep).size(), 1u);
  EXPECT_EQ(db_->purge_orphan_transcripts(), 0);

  const auto reused = makeEvent(1750000000000);
  EXPECT_EQ(reused, doomed_event);  // MAX(id)+1 reuses the freed id
  EXPECT_TRUE(db_->get_transcripts_for_event(reused).empty());
}

TEST_F(TranscriptDbTest, RemovingARecurringOccurrenceThroughCommitScheduleChangeCascades) {
  DuckEventSeries series;
  series.name = std::string{"Weekly"};
  series.start_date = 1730000000000;
  series.end_date = 1730003600000;
  series.duration = 3600;
  series.recurrence_rule = "FREQ=WEEKLY;INTERVAL=1;BYDAY=TU";
  series.event_stat_id = 1;
  series.payment_stat_id = 1;
  const auto series_id = db_->add_event_series(series);
  ASSERT_GT(series_id, 0);

  DuckEvent occurrence;
  occurrence.name = std::string{"Materialized occurrence"};
  occurrence.start_date = 1730100000000;
  occurrence.end_date = 1730103600000;
  occurrence.duration = 3600;
  occurrence.event_stat_id = 1;
  occurrence.payment_stat_id = 1;
  occurrence.series_id = series_id;
  occurrence.original_occurrence_start = 1730100000000;
  const auto event_id = db_->add_event(occurrence);
  ASSERT_GT(event_id, 0);
  const auto t = db_->add_transcript(event_id, "live_local_v1");
  ASSERT_GT(db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "x")), 0);

  // Same shape as QTimelineModel::removeEvent for a materialized occurrence.
  const auto commit = db_->commit_schedule_change(
      [&]() -> std::optional<int64_t> {
        if (!db_->add_event_series_exception(series_id, *occurrence.original_occurrence_start,
                                             "deleted")) {
          return std::nullopt;
        }
        if (!db_->remove_event(event_id)) return std::nullopt;
        return series_id;
      },
      "Europe/Moscow",
      [](const pcm::database::ScheduleSource &) { return std::optional<std::string>("p"); });
  ASSERT_TRUE(commit.has_value());
  EXPECT_EQ(db_->get_event(event_id), nullptr);
  EXPECT_EQ(db_->get_transcript(t), nullptr);
  EXPECT_TRUE(db_->get_transcript_phrases(t).empty());
}

TEST_F(TranscriptDbTest, DeletePhraseBumpsTranscriptUpdatedAt) {
  const auto t = db_->add_transcript(makeEvent(), "live_local_v1");
  const auto id = db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "x"));
  const auto before = db_->get_transcript(t)->updated_at;
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  ASSERT_TRUE(db_->delete_transcript_phrase(id));
  EXPECT_GT(db_->get_transcript(t)->updated_at, before);
}

TEST_F(TranscriptDbTest, AddPhraseRejectedAfterStatusLeavesRecording) {
  const auto t = db_->add_transcript(makeEvent(), "live_local_v1");
  ASSERT_TRUE(db_->set_transcript_status(t, "draft"));
  EXPECT_EQ(db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "late")), 0);
  EXPECT_EQ(db_->count_transcript_phrases(t), 0);
}

TEST_F(TranscriptDbTest, AddPhraseRejectedAfterConsentRevoked) {
  const auto t = db_->add_transcript(makeEvent(), "live_local_v1");
  ASSERT_TRUE(db_->revoke_transcript_consent(t));
  EXPECT_EQ(db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "late")), 0);
  EXPECT_EQ(db_->add_transcript_phrase(phrase(31337, "participant", "C", 0, 1000, "x")), 0);
  EXPECT_EQ(db_->count_transcript_phrases(t), 0);
}

TEST_F(TranscriptDbTest, DeleteTranscriptSucceedsWithCheckpointAndStillAllowsReopen) {
  const auto t = db_->add_transcript(makeEvent(), "live_local_v1");
  ASSERT_GT(db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "x")), 0);
  ASSERT_TRUE(db_->delete_transcript(t));
  ASSERT_TRUE(db_->delete_all_transcripts());
  EXPECT_EQ(db_->count_transcripts(), 0);
}

TEST_F(TranscriptDbTest, GetTranscriptsForClientFollowsEventClientLinks) {
  DuckClient client;
  client.name = std::string{"A"};
  const auto client_id = db_->add_client(client);
  DuckClient other;
  other.name = std::string{"B"};
  const auto other_id = db_->add_client(other);
  ASSERT_GT(client_id, 0);
  ASSERT_GT(other_id, 0);
  const auto e1 = makeEvent(1730000000000);
  const auto e2 = makeEvent(1740000000000);
  const auto e3 = makeEvent(1750000000000);
  ASSERT_GT(db_->add_event_client(e1, client_id), 0);
  ASSERT_GT(db_->add_event_client(e2, client_id), 0);
  ASSERT_GT(db_->add_event_client(e3, other_id), 0);
  const auto t2 = db_->add_transcript(e2, "live_local_v1");
  const auto t1 = db_->add_transcript(e1, "live_local_v1");
  db_->add_transcript(e3, "live_local_v1");

  const auto list = db_->get_transcripts_for_client(client_id);
  ASSERT_EQ(list.size(), 2u);
  EXPECT_EQ(list[0].id, t2);  // created first
  EXPECT_EQ(list[1].id, t1);
  EXPECT_TRUE(db_->get_transcripts_for_client(0).empty());
  EXPECT_TRUE(db_->get_transcripts_for_client(31337).empty());
}

TEST_F(TranscriptDbTest, CountsTranscriptsAndPhrases) {
  EXPECT_EQ(db_->count_transcripts(), 0);
  const auto t = db_->add_transcript(makeEvent(), "live_local_v1");
  db_->add_transcript(makeEvent(1750000000000), "live_local_v1");
  ASSERT_GT(db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "a")), 0);
  ASSERT_GT(db_->add_transcript_phrase(phrase(t, "participant", "C", 1000, 2000, "b")), 0);
  EXPECT_EQ(db_->count_transcripts(), 2);
  EXPECT_EQ(db_->count_transcript_phrases(t), 2);
  EXPECT_EQ(db_->count_transcript_phrases(31337), 0);
}

TEST_F(TranscriptDbTest, RenameSpeakerChangesOnlyThatRoleAndIsNotATextEdit) {
  const auto t = db_->add_transcript(makeEvent(), "live_local_v1");
  ASSERT_GT(db_->add_transcript_phrase(phrase(t, "participant", "Speaker 1", 0, 1000, "a")), 0);
  ASSERT_GT(db_->add_transcript_phrase(phrase(t, "participant", "Speaker 1", 1000, 2000, "b")), 0);
  ASSERT_GT(db_->add_transcript_phrase(phrase(t, "practitioner", "Me", 2000, 3000, "c")), 0);
  const auto before = db_->get_transcript(t)->updated_at;
  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  ASSERT_TRUE(db_->rename_transcript_speaker(t, "participant", "Anna"));
  const auto phrases = db_->get_transcript_phrases(t);
  ASSERT_EQ(phrases.size(), 3u);
  EXPECT_EQ(phrases[0].speaker_name.value_or(""), "Anna");
  EXPECT_EQ(phrases[1].speaker_name.value_or(""), "Anna");
  EXPECT_EQ(phrases[2].speaker_name.value_or(""), "Me");
  for (const auto &p : phrases) EXPECT_FALSE(p.edited);
  EXPECT_GT(db_->get_transcript(t)->updated_at, before);

  EXPECT_TRUE(db_->rename_transcript_speaker(t, "unknown_role", "X"));  // transcript exists
  EXPECT_FALSE(db_->rename_transcript_speaker(t, "participant", ""));
  EXPECT_FALSE(db_->rename_transcript_speaker(31337, "participant", "X"));
}

namespace {

// Records error-level log lines so a test can observe the release-build
// fallback path (asserts are compiled out there).
class ErrorCapture : public plog::IAppender {
 public:
  void write(const plog::Record &record) override {
    if (!active_ || record.getSeverity() > plog::error) return;
    std::lock_guard lock(mutex_);
    lines_.push_back(record.getMessage());
  }
  void begin() { active_ = true; }
  std::vector<std::string> end() {
    active_ = false;
    std::lock_guard lock(mutex_);
    return std::exchange(lines_, {});
  }

 private:
  std::atomic<bool> active_{false};
  std::mutex mutex_;
  std::vector<std::string> lines_;
};

ErrorCapture &errorCapture() {
  static ErrorCapture capture;
  static const bool installed = [] {
    if (auto *logger = plog::get<PLOG_DEFAULT_INSTANCE_ID>()) {
      logger->addAppender(&capture);
    } else {
      plog::init(plog::error, &capture);
    }
    return true;
  }();
  (void)installed;
  return capture;
}

}  // namespace

TEST_F(TranscriptDbTest, PhraseInsertWorksFromAnotherThreadDuringScheduleTransaction) {
  const auto t = db_->add_transcript(makeEvent(), "live_local_v1");
  ASSERT_GT(t, 0);

  auto &capture = errorCapture();
  capture.begin();
  int64_t id = 0;
  // The mutation runs inside the schedule transaction on this thread; the
  // writer thread must not join or trip over that transaction's connection.
  db_->commit_schedule_change(
      [&]() -> std::optional<int64_t> {
        std::thread writer([&] {
          id = db_->add_transcript_phrase(phrase(t, "participant", "Client", 1000, 2000, "hi"));
        });
        writer.join();
        return std::nullopt;
      },
      "Europe/Moscow",
      [](const pcm::database::ScheduleSource &) { return std::optional<std::string>("p"); });
  const auto errors = capture.end();

  EXPECT_GT(id, 0);
  EXPECT_TRUE(errors.empty()) << "unexpected error log: " << (errors.empty() ? "" : errors[0]);
  const auto phrases = db_->get_transcript_phrases(t);
  ASSERT_EQ(phrases.size(), 1u);
  EXPECT_EQ(phrases[0].text, "hi");
}
