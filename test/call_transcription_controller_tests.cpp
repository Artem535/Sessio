#include "call_side_panel.h"
#include "call_transcription_controller.h"
#include "config.h"
#include "fake_video_provider.h"
#include "transcript_panel.h"
#include "transcription_test_support.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <future>
#include <gtest/gtest.h>

using namespace pcm::transcriptionui;
using namespace pcm::transcription;
using namespace pcm::transcription::testing;
namespace {
template <class F> bool pump(F predicate) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 5000) {
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (predicate())
      return true;
    std::this_thread::yield();
  }
  return predicate();
}
class ControllerTest : public ::testing::Test {
protected:
  void SetUp() override {
    pcm::config::Config config{
        .db_conf = pcm::config::DatabaseConfig{
            .db_pth = Poco::Path(temp.path().toStdString())}};
    db = std::make_shared<pcm::database::Database>(config);
    DuckEvent event;
    event.name = "Test";
    event.start_date = 1730000000000;
    event.end_date = 1730003600000;
    event.duration = 3600;
    event.event_stat_id = 1;
    event.payment_stat_id = 1;
    eventId = db->add_event(event);
    provider = new pcm::video::test::FakeVideoProvider;
    video = std::make_unique<pcm::video::VideoSession>(provider);
    panel = new TranscriptPanel;
    side = std::make_unique<CallSidePanel>(new QWidget, panel);
    recognizer = std::make_shared<FakeRecognizer>();
    hooks.resolveEvent = [this](int64_t) {
      ++resolutions;
      return resolved ? std::optional<int64_t>(eventId) : std::nullopt;
    };
    hooks.askConsent = [this] {
      ++consents;
      return consent;
    };
    hooks.askRevokeChoice = [this] {
      ++choices;
      return deleteRecorded ? ControllerHooks::RevokeChoice::DeleteRecorded
                            : ControllerHooks::RevokeChoice::KeepAsDraft;
    };
    hooks.transcriptionEnabled = [this] { return enabled; };
    hooks.modelsAvailable = [this] { return models; };
    hooks.eventMaterialised = [this](int64_t id) { materialised = id; };
    hooks.engineFactory = [this] {
      auto rec = recognizer;
      auto count = tracks;
      return pcm::calltranscription::EngineFactory(
          [rec, count](EngineCallbacks cb, QString *) {
            return std::make_shared<TranscriptionEngine>(
                [count] {
                  ++*count;
                  return std::make_unique<FakeVad>();
                },
                rec, std::move(cb));
          });
    };
  }
  void TearDown() override {
    controller.reset();
    EXPECT_TRUE(pump([this] {
      for (auto &row : db->get_transcripts_for_event(eventId))
        if (row.status == "recording")
          return false;
      return true;
    }));
  }
  void make(std::optional<int64_t> id = {}) {
    controller = std::make_unique<CallTranscriptionController>(
        db, panel, side.get(), hooks);
    controller->attachCall(video.get(),
                           id ? id : std::optional<int64_t>(eventId));
  }
  void start() {
    make();
    controller->onTranscribeRequested();
  }
  int64_t transcriptId() {
    return db->get_transcripts_for_event(eventId).at(0).id;
  }
  QString label(const char *name) {
    return panel->findChild<QLabel *>(name)->text();
  }
  void phrase() {
    provider->simulateParticipantJoined({"remote", "Test", "", false});
    ASSERT_TRUE(pump([this] { return *tracks == 1; }));
    provider->simulateAudio("remote", concat({speech(1000), silence(800)}));
    ASSERT_TRUE(pump([this] { return panel->phraseCount() == 1; }));
  }
  QTemporaryDir temp;
  std::shared_ptr<pcm::database::Database> db;
  int64_t eventId{}, materialised{};
  int resolutions{}, consents{}, choices{};
  bool consent = true, enabled = true, models = true, resolved = true,
       deleteRecorded = true;
  std::shared_ptr<std::atomic<int>> tracks =
      std::make_shared<std::atomic<int>>(0);
  std::shared_ptr<FakeRecognizer> recognizer;
  pcm::video::test::FakeVideoProvider *provider{};
  std::unique_ptr<pcm::video::VideoSession> video;
  TranscriptPanel *panel{};
  std::unique_ptr<CallSidePanel> side;
  ControllerHooks hooks;
  std::unique_ptr<CallTranscriptionController> controller;
};
TEST_F(ControllerTest, ConsentRejectedCreatesNoTranscriptAndNoSink) {
  consent = false;
  start();
  EXPECT_EQ(db->count_transcripts(), 0);
  EXPECT_EQ(provider->audioSink(), nullptr);
  EXPECT_EQ(resolutions, 0);
}
TEST_F(ControllerTest, AudioGapNoticeFollowsSessionWithoutOverwritingErrors) {
  start();
  ASSERT_TRUE(pump([this] { return provider->audioSink() != nullptr; }));
  panel->setError("Model error");
  emit provider->audioInterrupted("local");
  EXPECT_FALSE(panel->findChild<QLabel *>("transcriptAudioGap")->isHidden());
  emit provider->audioResumed("local");
  EXPECT_TRUE(panel->findChild<QLabel *>("transcriptAudioGap")->isHidden());
  EXPECT_EQ(label("transcriptError"), "Model error");
}
TEST_F(ControllerTest,
       ConsentAcceptedCreatesRecordingTranscriptWithScopeAndEvent) {
  start();
  auto row = db->get_transcript(transcriptId());
  EXPECT_EQ(row->status, "recording");
  EXPECT_EQ(row->consent_scope, "live_local_v1");
  EXPECT_EQ(row->event_id, eventId);
  EXPECT_NE(provider->audioSink(), nullptr);
}
TEST_F(ControllerTest, VirtualEventIdIsResolvedAndTimelineHookCalled) {
  make(-5);
  controller->onTranscribeRequested();
  EXPECT_EQ(materialised, eventId);
  EXPECT_EQ(db->count_transcripts(), 1);
}
TEST_F(ControllerTest, UnresolvableEventShowsErrorAndStartsNothing) {
  resolved = false;
  start();
  EXPECT_EQ(db->count_transcripts(), 0);
  EXPECT_FALSE(label("transcriptError").isEmpty());
}
TEST_F(ControllerTest, DisabledSettingBlocksStartWithReason) {
  enabled = false;
  start();
  EXPECT_FALSE(controller->startAvailable());
  EXPECT_EQ(consents, 0);
  EXPECT_EQ(label("transcriptStartReason"),
            "Transcription is turned off in Settings");
}
TEST_F(ControllerTest, MissingModelsBlockStartWithReason) {
  models = false;
  start();
  EXPECT_FALSE(controller->startAvailable());
  EXPECT_EQ(consents, 0);
  EXPECT_EQ(label("transcriptStartReason"), "Speech models are not installed");
}
TEST_F(ControllerTest, CallWithoutEventBlocksStart) {
  make();
  controller->attachCall(video.get(), std::nullopt);
  controller->onTranscribeRequested();
  EXPECT_FALSE(controller->startAvailable());
  EXPECT_EQ(consents, 0);
}
TEST_F(ControllerTest, AvailabilityRefreshTracksSettingsModelsAndEvent) {
  make();
  EXPECT_TRUE(controller->startAvailable());
  enabled = false;
  controller->refreshAvailability();
  EXPECT_FALSE(controller->startAvailable());
  enabled = true;
  models = false;
  controller->refreshAvailability();
  EXPECT_FALSE(controller->startAvailable());
  models = true;
  controller->refreshAvailability();
  EXPECT_TRUE(controller->startAvailable());
  controller->detachCall();
  EXPECT_FALSE(controller->startAvailable());
}
TEST_F(ControllerTest, PhrasesReachThePanelAndTheDatabase) {
  start();
  phrase();
  EXPECT_EQ(db->count_transcript_phrases(transcriptId()), 1);
}
TEST_F(ControllerTest, StopFinishesAsDraftAndEmitsReady) {
  start();
  QSignalSpy ready(controller.get(),
                   &CallTranscriptionController::callTranscriptReady);
  panel->stopRequested();
  ASSERT_TRUE(pump([&] { return ready.count() == 1; }));
  EXPECT_EQ(db->get_transcript(transcriptId())->status, "draft");
  EXPECT_EQ(ready.at(0).at(0).toLongLong(), eventId);
}
TEST_F(ControllerTest, RevokeAskedChoiceDelete) {
  start();
  phrase();
  const auto id = transcriptId();
  panel->revokeRequested();
  ASSERT_TRUE(pump([&] { return choices == 1; }));
  EXPECT_EQ(db->get_transcript(id), nullptr);
  EXPECT_EQ(db->count_transcript_phrases(id), 0);
  EXPECT_EQ(panel->phraseCount(), 0);
}
TEST_F(ControllerTest, RevokeAskedChoiceKeep) {
  deleteRecorded = false;
  start();
  phrase();
  const auto id = transcriptId();
  panel->revokeRequested();
  ASSERT_TRUE(pump([&] { return choices == 1; }));
  EXPECT_TRUE(db->get_transcript(id)->consent_revoked_at.has_value());
  EXPECT_EQ(db->get_transcript(id)->status, "draft");
  EXPECT_EQ(panel->phraseCount(), 1);
}
TEST_F(ControllerTest, TrackFailureDoesNotExposeParticipantIdentity) {
  start();
  auto *session =
      controller->findChild<pcm::calltranscription::TranscriptionSession *>();
  ASSERT_NE(session, nullptr);
  session->trackFailed("private participant id");
  EXPECT_FALSE(label("transcriptNotice").isEmpty());
  EXPECT_FALSE(label("transcriptNotice").contains("private participant id"));
}
TEST_F(ControllerTest, NonterminalFailureStopsBeforeAllowingRestart) {
  start();
  auto *session =
      controller->findChild<pcm::calltranscription::TranscriptionSession *>();
  ASSERT_NE(session, nullptr);
  session->failed("Could not save the transcript");
  EXPECT_EQ(provider->audioSink(), nullptr);
  EXPECT_FALSE(controller->startAvailable());
  EXPECT_EQ(session->state(), pcm::calltranscription::SessionState::Stopping);
  ASSERT_TRUE(pump([&] { return controller->startAvailable(); }));
  EXPECT_EQ(db->get_transcript(transcriptId())->status, "draft");
}
TEST_F(ControllerTest, LeavingCallStopsTranscriptionWithoutProviderLeft) {
  start();
  QSignalSpy ready(controller.get(),
                   &CallTranscriptionController::callTranscriptReady);
  video->stateChanged(pcm::video::VideoSessionState::Leaving);
  EXPECT_EQ(provider->audioSink(), nullptr);
  ASSERT_TRUE(pump([&] { return ready.count() == 1; }));
  EXPECT_FALSE(controller->startAvailable());
}
TEST_F(ControllerTest, ShutdownInsideRevokeChoiceIsSafe) {
  hooks.askRevokeChoice = [this] {
    ++choices;
    QPointer<pcm::calltranscription::TranscriptionSession> sender =
        controller->findChild<pcm::calltranscription::TranscriptionSession *>();
    controller->shutdown();
    EXPECT_FALSE(sender.isNull());
    return ControllerHooks::RevokeChoice::DeleteRecorded;
  };
  start();
  const auto id = transcriptId();
  panel->revokeRequested();
  ASSERT_TRUE(pump([&] { return choices == 1; }));
  EXPECT_NE(db->get_transcript(id), nullptr);
  EXPECT_EQ(db->get_transcript(id)->status, "draft");
}
TEST_F(ControllerTest, OldSessionSignalsCannotUpdateReplacement) {
  start();
  QPointer<pcm::calltranscription::TranscriptionSession> old =
      controller->findChild<pcm::calltranscription::TranscriptionSession *>();
  QSignalSpy ready(controller.get(),
                   &CallTranscriptionController::callTranscriptReady);
  QObject::connect(controller.get(), &CallTranscriptionController::callTranscriptReady,
          controller.get(), [this, old](int64_t, int64_t) {
            EXPECT_FALSE(controller->startAvailable());
            controller->onTranscribeRequested();
            EXPECT_EQ(db->count_transcripts(), 1);
            ASSERT_FALSE(old.isNull());
            DuckTranscriptPhrase phrase;
            phrase.text = "stale";
            old->phraseAdded(phrase);
            old->trackFailed("private stale id");
          });
  panel->stopRequested();
  ASSERT_TRUE(pump([&] { return ready.count() == 1; }));
  ASSERT_TRUE(pump([&] { return old.isNull(); }));
  controller->onTranscribeRequested();
  EXPECT_EQ(db->count_transcripts(), 2);
  EXPECT_EQ(panel->phraseCount(), 0);
  EXPECT_TRUE(label("transcriptNotice").isEmpty());
  phrase();
  const auto rows = db->get_transcripts_for_event(eventId);
  EXPECT_EQ(db->count_transcript_phrases(rows.back().id), 1);
}
TEST_F(ControllerTest, ConsentCannotTransferToNewAttachment) {
  auto *otherProvider = new pcm::video::test::FakeVideoProvider;
  pcm::video::VideoSession other(otherProvider);
  hooks.askConsent = [this, &other] {
    ++consents;
    if (consents == 1)
      controller->attachCall(&other, eventId);
    return true;
  };
  start();
  EXPECT_EQ(db->count_transcripts(), 0);
  EXPECT_EQ(otherProvider->audioSink(), nullptr);
  controller->onTranscribeRequested();
  EXPECT_EQ(consents, 2);
  EXPECT_EQ(db->count_transcripts(), 1);
  EXPECT_NE(otherProvider->audioSink(), nullptr);
}
TEST_F(ControllerTest, ResolveCannotTransferConsentToNewAttachment) {
  hooks.resolveEvent = [this](int64_t) {
    controller->attachCall(video.get(), eventId);
    return std::optional<int64_t>(eventId);
  };
  start();
  EXPECT_EQ(db->count_transcripts(), 0);
}
TEST_F(ControllerTest, MaterialisationCannotTransferConsentToNewAttachment) {
  hooks.eventMaterialised = [this](int64_t) {
    controller->attachCall(video.get(), eventId);
  };
  make(-5);
  controller->onTranscribeRequested();
  EXPECT_EQ(db->count_transcripts(), 0);
}
TEST_F(ControllerTest, DestructionInsideRevokeKeepsSenderAliveUntilReturn) {
  QPointer<pcm::calltranscription::TranscriptionSession> sender;
  hooks.askRevokeChoice = [this, &sender] {
    ++choices;
    sender = controller->findChild<pcm::calltranscription::TranscriptionSession *>();
    controller.reset();
    EXPECT_FALSE(sender.isNull());
    return ControllerHooks::RevokeChoice::DeleteRecorded;
  };
  start();
  panel->revokeRequested();
  ASSERT_TRUE(pump([&] { return choices == 1; }));
  EXPECT_TRUE(sender.isNull());
}
TEST_F(ControllerTest, DestructionInsideConsentStartsNothing) {
  hooks.askConsent = [this] {
    controller.reset();
    return true;
  };
  start();
  EXPECT_EQ(db->count_transcripts(), 0);
  EXPECT_EQ(provider->audioSink(), nullptr);
}
TEST_F(ControllerTest, FailureIsShownAsUserFacingText) {
  hooks.engineFactory = [] {
    return pcm::calltranscription::EngineFactory(
        [](EngineCallbacks, QString *error) {
          *error = "private diagnostic";
          return std::shared_ptr<TranscriptionEngine>{};
        });
  };
  start();
  ASSERT_TRUE(pump([&] { return !label("transcriptError").isEmpty(); }));
  for (auto *item : panel->findChildren<QLabel *>())
    EXPECT_FALSE(item->text().contains("private diagnostic"));
}
TEST_F(ControllerTest, CallEndedStopsSessionAndKeepsResultVisible) {
  start();
  phrase();
  QSignalSpy ready(controller.get(),
                   &CallTranscriptionController::callTranscriptReady);
  controller->detachCall();
  ASSERT_TRUE(pump([&] { return ready.count() == 1; }));
  EXPECT_EQ(panel->phraseCount(), 1);
  EXPECT_EQ(provider->audioSink(), nullptr);
}
TEST_F(ControllerTest, SecondStartWhileFinishingIsRejected) {
  auto release = std::make_shared<std::promise<void>>();
  auto gate = release->get_future().share();
  recognizer = std::make_shared<FakeRecognizer>([gate](int, size_t) {
    gate.wait();
    return "phrase";
  });
  start();
  provider->simulateParticipantJoined({"remote", "Test", "", false});
  ASSERT_TRUE(pump([&] { return *tracks == 1; }));
  provider->simulateAudio("remote", concat({speech(1000), silence(800)}));
  ASSERT_TRUE(pump([&] { return recognizer->calls() == 1; }));
  panel->stopRequested();
  controller->onTranscribeRequested();
  EXPECT_EQ(consents, 1);
  EXPECT_FALSE(label("transcriptNotice").isEmpty());
  release->set_value();
}
TEST_F(ControllerTest, ShutdownDoesNotBlock) {
  auto release = std::make_shared<std::promise<void>>();
  auto gate = release->get_future().share();
  recognizer = std::make_shared<FakeRecognizer>([gate](int, size_t) {
    gate.wait();
    return "phrase";
  });
  start();
  provider->simulateParticipantJoined({"remote", "Test", "", false});
  ASSERT_TRUE(pump([&] { return *tracks == 1; }));
  provider->simulateAudio("remote", concat({speech(1000), silence(800)}));
  ASSERT_TRUE(pump([&] { return recognizer->calls() == 1; }));
  QElapsedTimer timer;
  timer.start();
  controller->shutdown();
  EXPECT_LT(timer.elapsed(), 200);
  EXPECT_EQ(provider->audioSink(), nullptr);
  release->set_value();
}
}
int main(int argc, char **argv) {
  QTemporaryDir home;
  qputenv("HOME", home.path().toUtf8());
  qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
