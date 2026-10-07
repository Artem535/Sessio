#include <Poco/File.h>
#include <Poco/Path.h>
#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

#include "config.h"
#include "database.h"
#include "fake_video_provider.h"
#include "transcription_session.h"
#include "transcription_test_support.h"

using namespace pcm::calltranscription;
using namespace pcm::transcription;
using namespace pcm::transcription::testing;
using pcm::video::Participant;
using pcm::video::test::FakeVideoProvider;

namespace {

template <typename Pred>
bool pump(Pred pred, int timeoutMs = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeoutMs) {
    QCoreApplication::processEvents();
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  QCoreApplication::processEvents();
  return pred();
}

struct Probe {
  std::mutex mutex;
  std::set<std::thread::id> factoryThreads;
  std::set<std::thread::id> vadThreads;
  std::atomic<int> vadCount{0};
  bool factoryCalled = false;
};

class SessionTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
    dir_ = Poco::Path(Poco::Path::current())
               .append(std::string("tmp_session_") + info->name())
               .toString();
    removeDir();
    pcm::config::Config conf{
        .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir_)}};
    db_ = std::make_shared<pcm::database::Database>(conf);
    DuckEvent event;
    event.name = std::string{"Session"};
    event.start_date = 1730000000000;
    event.end_date = 1730003600000;
    event.duration = 3600;
    event.event_stat_id = 1;
    event.payment_stat_id = 1;
    eventId_ = db_->add_event(event);
    ASSERT_GT(eventId_, 0);
    recognizer_ = std::make_shared<FakeRecognizer>();
  }

  void TearDown() override {
    session_.reset();
    provider_.reset();
    db_.reset();
    removeDir();
  }

  void removeDir() {
    Poco::File dir(dir_);
    if (dir.exists()) dir.remove(true);
  }

  EngineFactory factory() {
    return [this](EngineCallbacks cb, QString *) {
      {
        std::lock_guard lock(probe_.mutex);
        probe_.factoryThreads.insert(std::this_thread::get_id());
        probe_.factoryCalled = true;
      }
      return std::make_shared<TranscriptionEngine>(
          [this] {
            std::lock_guard lock(probe_.mutex);
            probe_.vadThreads.insert(std::this_thread::get_id());
            ++probe_.vadCount;
            return std::make_unique<FakeVad>();
          },
          recognizer_, std::move(cb));
    };
  }

  void make(EngineFactory f = {}) {
    provider_ = std::make_unique<FakeVideoProvider>();
    session_ = std::make_unique<TranscriptionSession>(db_, provider_.get(),
                                                      f ? std::move(f) : factory());
  }

  void join(const QString &id, const QString &name, bool local) {
    provider_->simulateParticipantJoined({id, name, "", local});
  }

  void startAndWaitRecording(int expectedTracks) {
    ASSERT_TRUE(session_->start(eventId_, "live_local_v1"));
    ASSERT_TRUE(pump([&] { return session_->state() == SessionState::Recording; }));
    ASSERT_TRUE(pump([&] { return probe_.vadCount >= expectedTracks; }));
  }

  void feedPhrase(const QString &id, bool closeWithSilence = true) {
    provider_->simulateAudio(id, closeWithSilence ? concat({speech(1000), silence(800)})
                                                  : speech(1000));
  }

  int64_t phraseCount() { return db_->count_transcript_phrases(session_->transcriptId()); }

  std::string dir_;
  std::shared_ptr<pcm::database::Database> db_;
  int64_t eventId_ = 0;
  Probe probe_;
  std::shared_ptr<FakeRecognizer> recognizer_;
  std::unique_ptr<FakeVideoProvider> provider_;
  std::unique_ptr<TranscriptionSession> session_;
};

} // namespace

TEST_F(SessionTest, StartCreatesRecordingTranscriptAndInstallsSink) {
  make();
  EXPECT_EQ(provider_->audioSink(), nullptr);
  ASSERT_TRUE(session_->start(eventId_, "live_local_v1"));
  ASSERT_GT(session_->transcriptId(), 0);
  EXPECT_NE(provider_->audioSink(), nullptr);
  const auto t = db_->get_transcript(session_->transcriptId());
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->status, "recording");
  EXPECT_EQ(t->consent_scope, "live_local_v1");
  EXPECT_EQ(t->event_id, eventId_);
}

TEST_F(SessionTest, StartRejectsEmptyConsentScopeAndNonPositiveEvent) {
  make();
  EXPECT_FALSE(session_->start(eventId_, ""));
  EXPECT_FALSE(session_->start(0, "live_local_v1"));
  EXPECT_FALSE(session_->start(-5, "live_local_v1"));
  EXPECT_EQ(session_->state(), SessionState::Idle);
  EXPECT_EQ(db_->count_transcripts(), 0);
  EXPECT_EQ(provider_->audioSink(), nullptr);
  EXPECT_FALSE(probe_.factoryCalled);
}

TEST_F(SessionTest, NoSinkInstalledBeforeStartOrAfterStop) {
  make();
  EXPECT_EQ(provider_->audioSink(), nullptr);
  startAndWaitRecording(0);
  QSignalSpy finished(session_.get(), &TranscriptionSession::finished);
  session_->stop();
  EXPECT_EQ(provider_->audioSink(), nullptr);
  ASSERT_TRUE(pump([&] { return finished.count() == 1; }));
  EXPECT_EQ(provider_->audioSink(), nullptr);
}

TEST_F(SessionTest, ModelLoadRunsOffTheGuiThread) {
  make();
  join("remote", "Anna", false);
  startAndWaitRecording(1);
  std::lock_guard lock(probe_.mutex);
  ASSERT_EQ(probe_.factoryThreads.size(), 1u);
  EXPECT_NE(*probe_.factoryThreads.begin(), std::this_thread::get_id());
  ASSERT_FALSE(probe_.vadThreads.empty());
  for (const auto &id : probe_.vadThreads) EXPECT_NE(id, std::this_thread::get_id());
}

TEST_F(SessionTest, ReachesRecordingAfterLoad) {
  make();
  qRegisterMetaType<SessionState>();
  QSignalSpy states(session_.get(), &TranscriptionSession::stateChanged);
  ASSERT_TRUE(session_->start(eventId_, "live_local_v1"));
  EXPECT_EQ(session_->state(), SessionState::Loading);
  ASSERT_TRUE(pump([&] { return session_->state() == SessionState::Recording; }));
  ASSERT_EQ(states.count(), 2);
  EXPECT_EQ(states.at(0).at(0).value<SessionState>(), SessionState::Loading);
  EXPECT_EQ(states.at(1).at(0).value<SessionState>(), SessionState::Recording);
}

TEST_F(SessionTest, LoadFailureEntersFailedStateAndFinalisesTranscript) {
  make([](EngineCallbacks, QString *error) {
    *error = "model missing";
    return std::shared_ptr<TranscriptionEngine>{};
  });
  QSignalSpy failed(session_.get(), &TranscriptionSession::failed);
  ASSERT_TRUE(session_->start(eventId_, "live_local_v1"));
  ASSERT_TRUE(pump([&] { return failed.count() == 1; }));
  EXPECT_EQ(failed.at(0).at(0).toString(), "model missing");
  EXPECT_EQ(session_->state(), SessionState::Failed);
  EXPECT_EQ(db_->get_transcript(session_->transcriptId())->status, "draft");
  EXPECT_EQ(provider_->audioSink(), nullptr);
}

TEST_F(SessionTest, PhrasesFromRemoteAndLocalTracksAreStoredWithRoles) {
  make();
  join("local", "Dr Who", true);
  join("remote", "Anna", false);
  QSignalSpy added(session_.get(), &TranscriptionSession::phraseAdded);
  startAndWaitRecording(2);
  feedPhrase("local");
  feedPhrase("remote");
  ASSERT_TRUE(pump([&] { return phraseCount() == 2; }));
  ASSERT_TRUE(pump([&] { return added.count() == 2; }));

  const auto rows = db_->get_transcript_phrases(session_->transcriptId());
  ASSERT_EQ(rows.size(), 2u);
  for (const auto &r : rows) {
    EXPECT_GT(r.id, 0);
    EXPECT_LE(r.start_ms, r.end_ms);
    if (r.track_role == "practitioner") {
      EXPECT_EQ(r.speaker_name, std::optional<std::string>{"Dr Who"});
    } else {
      EXPECT_EQ(r.track_role, "participant");
      EXPECT_EQ(r.speaker_name, std::optional<std::string>{"Anna"});
    }
  }
  EXPECT_NE(rows[0].track_role, rows[1].track_role);
  for (int i = 0; i < added.count(); ++i)
    EXPECT_GT(added.at(i).at(0).value<DuckTranscriptPhrase>().id, 0);
}

TEST_F(SessionTest, TimesAreNonDecreasingPerTrack) {
  make();
  join("remote", "Anna", false);
  startAndWaitRecording(1);
  feedPhrase("remote");
  feedPhrase("remote");
  ASSERT_TRUE(pump([&] { return phraseCount() == 2; }));
  const auto rows = db_->get_transcript_phrases(session_->transcriptId());
  EXPECT_LE(rows[0].end_ms, rows[1].start_ms);
  EXPECT_LE(rows[0].start_ms, rows[1].start_ms);
}

TEST_F(SessionTest, LateParticipantGetsTrackWithCallClockOffset) {
  make();
  join("local", "Dr Who", true);
  startAndWaitRecording(1);
  QTest::qWait(1000);
  join("late", "Boris", false);
  ASSERT_TRUE(pump([&] { return probe_.vadCount >= 2; }));
  feedPhrase("late");
  ASSERT_TRUE(pump([&] { return phraseCount() == 1; }));
  EXPECT_GE(db_->get_transcript_phrases(session_->transcriptId())[0].start_ms, 1000);
}

TEST_F(SessionTest, ParticipantJoinedWhileLoadingGetsTrackAfterLoad) {
  std::atomic<bool> release{false};
  make([this, &release](EngineCallbacks cb, QString *error) {
    while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return factory()(std::move(cb), error);
  });
  ASSERT_TRUE(session_->start(eventId_, "live_local_v1"));
  join("early", "Clara", false);
  EXPECT_EQ(session_->state(), SessionState::Loading);
  release = true;
  ASSERT_TRUE(pump([&] { return session_->state() == SessionState::Recording; }));
  ASSERT_TRUE(pump([&] { return probe_.vadCount >= 1; }));
  feedPhrase("early");
  ASSERT_TRUE(pump([&] { return phraseCount() == 1; }));
}

TEST_F(SessionTest, LeavingParticipantFlushesOpenPhrase) {
  make();
  join("remote", "Anna", false);
  join("other", "Boris", false);
  startAndWaitRecording(2);
  feedPhrase("remote", false);  // stays open: no trailing silence
  feedPhrase("other");
  // Boris' closed phrase arriving proves the segmenter has seen Anna's audio too.
  ASSERT_TRUE(pump([&] { return phraseCount() == 1; }));
  const auto rows = db_->get_transcript_phrases(session_->transcriptId());
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].speaker_name, std::optional<std::string>{"Boris"});
  provider_->simulateParticipantLeft("remote");
  ASSERT_TRUE(pump([&] { return phraseCount() == 2; }));
}

TEST_F(SessionTest, StopFinalisesDraftAndEmitsFinished) {
  make();
  join("remote", "Anna", false);
  startAndWaitRecording(1);
  feedPhrase("remote");
  ASSERT_TRUE(pump([&] { return phraseCount() == 1; }));
  QSignalSpy finished(session_.get(), &TranscriptionSession::finished);
  session_->stop();
  EXPECT_EQ(session_->state(), SessionState::Stopping);
  ASSERT_TRUE(pump([&] { return finished.count() == 1; }));
  EXPECT_EQ(finished.at(0).at(0).toLongLong(), session_->transcriptId());
  EXPECT_EQ(session_->state(), SessionState::Finished);
  EXPECT_EQ(db_->get_transcript(session_->transcriptId())->status, "draft");
  // The sink is gone, so this audio cannot reach the engine at all.
  EXPECT_EQ(provider_->audioSink(), nullptr);
  provider_->simulateAudio("remote", concat({speech(1000), silence(800)}));
  EXPECT_EQ(phraseCount(), 1);
}

TEST_F(SessionTest, StopFinalisesWriterJoined) {
  make();
  join("remote", "Anna", false);
  startAndWaitRecording(1);
  QSignalSpy added(session_.get(), &TranscriptionSession::phraseAdded);
  QSignalSpy finished(session_.get(), &TranscriptionSession::finished);
  feedPhrase("remote");
  session_->stop();
  ASSERT_TRUE(pump([&] { return finished.count() == 1; }));
  // finished is queued after the writer joined: every stored phrase has already
  // been announced, and nothing is stored or announced afterwards.
  const auto stored = phraseCount();
  EXPECT_EQ(stored, 1);
  EXPECT_EQ(added.count(), stored);
  QCoreApplication::processEvents();
  EXPECT_EQ(phraseCount(), stored);
  EXPECT_EQ(added.count(), stored);
}

TEST_F(SessionTest, StopDrainsOpenPhraseBeforeFinishing) {
  make();
  join("remote", "Anna", false);
  startAndWaitRecording(1);
  feedPhrase("remote");
  QSignalSpy finished(session_.get(), &TranscriptionSession::finished);
  session_->stop();
  ASSERT_TRUE(pump([&] { return finished.count() == 1; }));
  EXPECT_EQ(phraseCount(), 1);
}

TEST_F(SessionTest, RevokeAbortsAndMarksConsentRevoked) {
  make();
  join("remote", "Anna", false);
  startAndWaitRecording(1);
  QSignalSpy revoked(session_.get(), &TranscriptionSession::revoked);
  feedPhrase("remote");
  session_->revoke();
  EXPECT_EQ(provider_->audioSink(), nullptr);
  ASSERT_TRUE(pump([&] { return revoked.count() == 1; }));
  EXPECT_EQ(session_->state(), SessionState::Finished);
  EXPECT_TRUE(db_->get_transcript(session_->transcriptId())->consent_revoked_at.has_value());
  const auto after = phraseCount();  // the phrase fed before revoke() may or may not have landed
  provider_->simulateAudio("remote", concat({speech(1000), silence(800)}));  // no sink
  EXPECT_EQ(phraseCount(), after);
}

TEST_F(SessionTest, ProviderLeftAutoStops) {
  make();
  startAndWaitRecording(0);
  QSignalSpy finished(session_.get(), &TranscriptionSession::finished);
  provider_->simulateLeft();
  ASSERT_TRUE(pump([&] { return finished.count() == 1; }));
  EXPECT_EQ(db_->get_transcript(session_->transcriptId())->status, "draft");
}

TEST_F(SessionTest, ProviderConnectionLostAutoStops) {
  make();
  startAndWaitRecording(0);
  QSignalSpy finished(session_.get(), &TranscriptionSession::finished);
  provider_->simulateConnectionLost("network");
  ASSERT_TRUE(pump([&] { return finished.count() == 1; }));
  EXPECT_EQ(session_->state(), SessionState::Finished);
}

TEST_F(SessionTest, DestroyingSessionWhileRecordingFinalisesAndDoesNotCrash) {
  make();
  join("remote", "Anna", false);
  startAndWaitRecording(1);
  feedPhrase("remote", false);
  const auto id = session_->transcriptId();
  session_.reset();
  EXPECT_EQ(provider_->audioSink(), nullptr);
  // Finalised by the detached cleanup, not by the destructor itself.
  ASSERT_TRUE(waitFor([&] { return db_->get_transcript(id)->status == "draft"; }));
  provider_->simulateAudio("remote", speech(100));  // no session, must be dropped
}

TEST_F(SessionTest, DestroyingSessionWhileLoadingFinalises) {
  std::atomic<bool> inFactory{false};
  make([&inFactory](EngineCallbacks, QString *) {
    inFactory = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return std::shared_ptr<TranscriptionEngine>{};
  });
  ASSERT_TRUE(session_->start(eventId_, "live_local_v1"));
  const auto id = session_->transcriptId();
  ASSERT_TRUE(pump([&] { return inFactory.load(); }));
  session_.reset();
  ASSERT_TRUE(waitFor([&] { return db_->get_transcript(id)->status == "draft"; }));
}

TEST_F(SessionTest, GuiThreadIsNotBlockedByStop) {
  recognizer_ = std::make_shared<FakeRecognizer>([](int, size_t) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return std::string("slow");
  });
  make();
  join("remote", "Anna", false);
  startAndWaitRecording(1);
  feedPhrase("remote");
  ASSERT_TRUE(pump([&] { return recognizer_->calls() >= 1; }));
  QElapsedTimer timer;
  timer.start();
  session_->stop();
  EXPECT_LT(timer.elapsed(), 100);
  QSignalSpy finished(session_.get(), &TranscriptionSession::finished);
  ASSERT_TRUE(pump([&] { return finished.count() == 1; }, 10000));
}


namespace {
std::shared_ptr<FakeRecognizer> slowRecognizer(int ms) {
  return std::make_shared<FakeRecognizer>([ms](int, size_t) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    return std::string("slow");
  });
}
} // namespace

TEST_F(SessionTest, RevokeDuringLoadingAbortsAndRevokesConsent) {
  std::atomic<bool> release{false};
  make([this, &release](EngineCallbacks cb, QString *error) {
    while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return factory()(std::move(cb), error);
  });
  QSignalSpy revoked(session_.get(), &TranscriptionSession::revoked);
  QSignalSpy finished(session_.get(), &TranscriptionSession::finished);
  ASSERT_TRUE(session_->start(eventId_, "live_local_v1"));
  session_->revoke();
  EXPECT_EQ(session_->state(), SessionState::Stopping);
  release = true;
  ASSERT_TRUE(pump([&] { return revoked.count() == 1; }));
  EXPECT_EQ(finished.count(), 0);
  EXPECT_EQ(session_->state(), SessionState::Finished);
  EXPECT_TRUE(db_->get_transcript(session_->transcriptId())->consent_revoked_at.has_value());
  EXPECT_EQ(phraseCount(), 0);
}

TEST_F(SessionTest, RevokeDuringGracefulStopUpgradesToRevoked) {
  recognizer_ = slowRecognizer(400);
  make();
  join("remote", "Anna", false);
  startAndWaitRecording(1);
  feedPhrase("remote");
  ASSERT_TRUE(pump([&] { return recognizer_->calls() >= 1; }));
  QSignalSpy revoked(session_.get(), &TranscriptionSession::revoked);
  QSignalSpy finished(session_.get(), &TranscriptionSession::finished);
  session_->stop();
  session_->revoke();  // the user changes their mind while the drain is running
  ASSERT_TRUE(pump([&] { return revoked.count() == 1; }, 10000));
  EXPECT_EQ(finished.count(), 0);
  EXPECT_EQ(session_->state(), SessionState::Finished);
  EXPECT_TRUE(db_->get_transcript(session_->transcriptId())->consent_revoked_at.has_value());
  EXPECT_EQ(phraseCount(), 0);
}

TEST_F(SessionTest, NoPhraseIsWrittenOnceRevokeWasRequested) {
  recognizer_ = slowRecognizer(300);
  make();
  join("remote", "Anna", false);
  startAndWaitRecording(1);
  feedPhrase("remote");
  feedPhrase("remote");
  ASSERT_TRUE(pump([&] { return recognizer_->calls() >= 1; }));  // one phrase is mid-decode
  QSignalSpy revoked(session_.get(), &TranscriptionSession::revoked);
  session_->revoke();
  EXPECT_EQ(phraseCount(), 0);
  ASSERT_TRUE(pump([&] { return revoked.count() == 1; }, 10000));
  EXPECT_EQ(phraseCount(), 0);
}

TEST_F(SessionTest, StopDuringLoadingThenFactoryFailureEndsFinishedAsDraft) {
  std::atomic<bool> release{false};
  make([&release](EngineCallbacks, QString *error) {
    while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    *error = "model missing";
    return std::shared_ptr<TranscriptionEngine>{};
  });
  QSignalSpy finished(session_.get(), &TranscriptionSession::finished);
  QSignalSpy failed(session_.get(), &TranscriptionSession::failed);
  ASSERT_TRUE(session_->start(eventId_, "live_local_v1"));
  session_->stop();
  release = true;
  ASSERT_TRUE(pump([&] { return finished.count() == 1; }));
  EXPECT_EQ(session_->state(), SessionState::Finished);
  EXPECT_EQ(db_->get_transcript(session_->transcriptId())->status, "draft");
  EXPECT_EQ(failed.count(), 0);  // the user asked to stop; the load error is not surfaced
  EXPECT_EQ(provider_->audioSink(), nullptr);
}

TEST_F(SessionTest, DestructorDoesNotBlockOnSlowDecodeAndRowIsFinalisedLater) {
  recognizer_ = slowRecognizer(500);
  make();
  join("remote", "Anna", false);
  startAndWaitRecording(1);
  feedPhrase("remote");
  ASSERT_TRUE(pump([&] { return recognizer_->calls() >= 1; }));
  const auto id = session_->transcriptId();
  QElapsedTimer timer;
  timer.start();
  session_.reset();
  EXPECT_LT(timer.elapsed(), 200);
  ASSERT_TRUE(waitFor([&] { return db_->get_transcript(id)->status == "draft"; },
                      std::chrono::seconds(10)));
}

TEST_F(SessionTest, DestructorDoesNotBlockOnModelLoadAndRowIsFinalisedLater) {
  make([this](EngineCallbacks cb, QString *error) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return factory()(std::move(cb), error);
  });
  ASSERT_TRUE(session_->start(eventId_, "live_local_v1"));
  const auto id = session_->transcriptId();
  QElapsedTimer timer;
  timer.start();
  session_.reset();
  EXPECT_LT(timer.elapsed(), 200);
  ASSERT_TRUE(waitFor([&] { return db_->get_transcript(id)->status == "draft"; },
                      std::chrono::seconds(10)));
}

TEST_F(SessionTest, DestructorDoesNotBlockOnGracefulStopInFlight) {
  recognizer_ = slowRecognizer(500);
  make();
  join("remote", "Anna", false);
  startAndWaitRecording(1);
  feedPhrase("remote");
  ASSERT_TRUE(pump([&] { return recognizer_->calls() >= 1; }));
  const auto id = session_->transcriptId();
  session_->stop();
  QElapsedTimer timer;
  timer.start();
  session_.reset();
  EXPECT_LT(timer.elapsed(), 200);
  ASSERT_TRUE(waitFor([&] { return db_->get_transcript(id)->status == "draft"; },
                      std::chrono::seconds(10)));
}

TEST_F(SessionTest, RejoinWithSameIdRightAwayIsTranscribed) {
  make();
  join("remote", "Anna", false);
  startAndWaitRecording(1);
  feedPhrase("remote");
  ASSERT_TRUE(pump([&] { return phraseCount() == 1; }));
  provider_->simulateParticipantLeft("remote");
  join("remote", "Anna", false);  // immediately, before the engine erased the old track
  ASSERT_TRUE(pump([&] { return probe_.vadCount >= 2; }));
  feedPhrase("remote");
  ASSERT_TRUE(pump([&] { return phraseCount() == 2; }));
  for (const auto &r : db_->get_transcript_phrases(session_->transcriptId()))
    EXPECT_EQ(r.speaker_name, std::optional<std::string>{"Anna"});
}

TEST_F(SessionTest, DuplicateJoinSignalDoesNotAddSecondTrack) {
  make();
  join("remote", "Anna", false);
  startAndWaitRecording(1);
  emit provider_->participantJoined("remote");  // already present
  join("other", "Boris", false);                // queued after the duplicate
  ASSERT_TRUE(pump([&] { return probe_.vadCount >= 2; }));
  EXPECT_EQ(probe_.vadCount, 2);
}

TEST_F(SessionTest, StoreFailuresReportFailedExactlyOnce) {
  make();
  join("remote", "Anna", false);
  startAndWaitRecording(1);
  QSignalSpy failed(session_.get(), &TranscriptionSession::failed);
  // The database rejects phrases for a transcript that is no longer "recording".
  ASSERT_TRUE(db_->set_transcript_status(session_->transcriptId(), "draft"));
  for (int i = 0; i < 5; ++i) feedPhrase("remote");
  ASSERT_TRUE(pump([&] { return recognizer_->calls() >= 5; }));
  QSignalSpy finished(session_.get(), &TranscriptionSession::finished);
  session_->stop();  // joins the writer, so every store attempt has happened
  ASSERT_TRUE(pump([&] { return finished.count() == 1; }));
  EXPECT_EQ(failed.count(), 1);
  EXPECT_EQ(phraseCount(), 0);
}

TEST_F(SessionTest, AddTrackFailureIsReportedPerParticipantAndNotFatal) {
  make([this](EngineCallbacks cb, QString *) {
    return std::make_shared<TranscriptionEngine>(
        [this]() -> std::unique_ptr<IVoiceActivityDetector> {
          if (probe_.vadCount++ == 0) throw std::runtime_error("vad model broken");
          return std::make_unique<FakeVad>();
        },
        recognizer_, std::move(cb));
  });
  join("bad", "Anna", false);
  join("good", "Boris", false);
  QSignalSpy trackFailed(session_.get(), &TranscriptionSession::trackFailed);
  QSignalSpy failed(session_.get(), &TranscriptionSession::failed);
  ASSERT_TRUE(session_->start(eventId_, "live_local_v1"));
  ASSERT_TRUE(pump([&] { return trackFailed.count() == 1; }));
  EXPECT_EQ(trackFailed.at(0).at(0).toString(), "bad");
  EXPECT_EQ(failed.count(), 0);
  EXPECT_EQ(session_->state(), SessionState::Recording);
  feedPhrase("good");
  ASSERT_TRUE(pump([&] { return phraseCount() == 1; }));
}

TEST_F(SessionTest, DelayedChangedIsEmitted) {
  recognizer_ = slowRecognizer(250);
  make([this](EngineCallbacks cb, QString *) {
    EngineConfig cfg;
    cfg.delayed_after = std::chrono::milliseconds(50);
    return std::make_shared<TranscriptionEngine>(
        [this] {
          ++probe_.vadCount;
          return std::make_unique<FakeVad>();
        },
        recognizer_, std::move(cb), cfg);
  });
  join("remote", "Anna", false);
  QSignalSpy delayed(session_.get(), &TranscriptionSession::delayedChanged);
  startAndWaitRecording(1);
  for (int i = 0; i < 3; ++i) feedPhrase("remote");
  ASSERT_TRUE(pump([&] { return delayed.count() >= 1; }, 10000));
  EXPECT_TRUE(delayed.at(0).at(0).toBool());
}

TEST(SerialExecutorTest, RunsTasksInOrderOffTheCallingThread) {
  std::vector<int> order;
  std::thread::id worker;
  {
    SerialExecutor ex;
    for (int i = 0; i < 5; ++i)
      ex.post([&, i] {
        worker = std::this_thread::get_id();
        order.push_back(i);
      });
    ex.waitIdle();
    EXPECT_EQ(order.size(), 5u);
  }
  EXPECT_EQ(order, (std::vector<int>{0, 1, 2, 3, 4}));
  EXPECT_NE(worker, std::this_thread::get_id());
}

TEST(SerialExecutorTest, ThrowingTaskDoesNotStopWorkerAndDestructorDrains) {
  std::atomic<int> ran{0};
  {
    SerialExecutor ex;
    ex.post([] { throw std::runtime_error("x"); });
    ex.post([&] { ++ran; });
    ex.post([&] { ++ran; });
  }
  EXPECT_EQ(ran, 2);
}

// The session's executor tasks capture the shared state that owns the
// executor. When the GUI thread drops its reference first, the last one is
// released on the executor thread while it destroys the finished task; that
// must not join the executor's own thread (std::terminate on Windows).
TEST(SerialExecutorTest, ExecutorReleasedByItsOwnTaskDoesNotJoinItself) {
  struct Owner {
    SerialExecutor executor;
  };
  auto owner = std::make_shared<Owner>();
  std::weak_ptr<Owner> watch = owner;
  std::promise<void> started;
  std::promise<void> released;
  auto releasedFuture = released.get_future().share();
  owner->executor.post([keep = owner, &started, releasedFuture] {
    started.set_value();
    releasedFuture.wait();  // the caller has dropped its reference
  });
  started.get_future().wait();
  owner.reset();
  released.set_value();
  for (int i = 0; i < 500 && !watch.expired(); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_TRUE(watch.expired());
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
