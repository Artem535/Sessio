#include "call_side_panel.h"
#include "call_transcription_controller.h"
#include "config.h"
#include "engine_factory.h"
#include "fake_video_provider.h"
#include "transcript_page.h"
#include "transcript_panel.h"
#include "sherpa-onnx/c-api/cxx-api.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <gtest/gtest.h>

namespace {
template <typename Predicate> bool await(Predicate predicate, int timeoutMs = 15000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeoutMs) {
    QApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (predicate()) return true;
    std::this_thread::yield();
  }
  return predicate();
}
}

void runRealSpeechWorkflow(bool standalone) {
  const auto root = qEnvironmentVariable("SESSIO_MODELS_DIR");
  if (root.isEmpty() || !pcm::calltranscription::transcriptionModelsAvailable(
          QCoreApplication::applicationDirPath().toStdString(), root.toStdString()))
    GTEST_SKIP() << "Set SESSIO_MODELS_DIR to run the real-model workflow";
  const auto wave = sherpa_onnx::cxx::ReadWave(
      (root + "/gigaam-v3-rnnt/test_wavs/example.wav").toStdString());
  ASSERT_EQ(wave.sample_rate, 16000);
  ASSERT_FALSE(wave.samples.empty());
  std::vector<int16_t> stream(16000, 0); // one second of actual leading silence
  for (const auto sample : wave.samples)
    stream.push_back(static_cast<int16_t>(std::clamp(sample, -1.0f, 1.0f) * 32767));
  stream.resize(stream.size() + 32000, 0);
  QTemporaryDir storage;
  pcm::config::DatabaseConfig databaseConfig;
  databaseConfig.db_pth = Poco::Path(storage.path().toStdString());
  pcm::config::Config config;
  config.db_conf = databaseConfig;
  auto db = std::make_shared<pcm::database::Database>(config);
  DuckEvent event;
  event.name = "Synthetic speech workflow";
  event.start_date = 1730000000000;
  event.end_date = *event.start_date + 3600000;
  event.duration = 3600;
  event.event_stat_id = 1;
  event.payment_stat_id = 1;
  const std::optional<int64_t> eventId = standalone ? std::nullopt : std::optional<int64_t>(db->add_event(event));
  if (eventId) ASSERT_GT(*eventId, 0);
  auto *provider = new pcm::video::test::FakeVideoProvider;
  pcm::video::VideoSession call(provider);
  provider->simulateParticipantJoined({"specialist", "Specialist", "", true});
  provider->simulateParticipantJoined({"remote", "Participant", "", false});
  auto *panel = new TranscriptPanel;
  CallSidePanel side(new QWidget, panel);
  pcm::transcriptionui::ControllerHooks hooks;
  bool consent = false;
  hooks.askConsent = [&] { return consent; };
  hooks.resolveEvent = [=](int64_t) { return eventId; };
  hooks.transcriptionEnabled = [] { return true; };
  hooks.modelsAvailable = [] { return true; };
  hooks.engineFactory = [=] {
    return pcm::calltranscription::makeProductionEngineFactory(
        QCoreApplication::applicationDirPath().toStdString(), root.toStdString());
  };
  pcm::transcriptionui::CallTranscriptionController controller(db, panel, &side, hooks);
  controller.attachCall(&call, eventId);
  controller.onTranscribeRequested();
  EXPECT_EQ(db->count_transcripts(), 0);
  EXPECT_EQ(provider->audioSink(), nullptr);
  consent = true;
  controller.onTranscribeRequested();
  ASSERT_TRUE(await([&] {
    auto *session = controller.findChild<pcm::calltranscription::TranscriptionSession *>();
    return session && session->state() == pcm::calltranscription::SessionState::Recording;
  }));
  // QTimer represents a real 100-ms audio source, not a synchronization sleep.
  size_t offset = 0;
  QTimer source;
  QObject::connect(&source, &QTimer::timeout, &controller, [&] {
    const auto end = std::min(offset + 1600, stream.size());
    const std::vector<int16_t> chunk(stream.begin() + offset, stream.begin() + end);
    provider->simulateAudio("specialist", chunk, 16000);
    provider->simulateAudio("remote", chunk, 16000);
    offset = end;
    if (offset == stream.size()) source.stop();
  });
  source.start(100);
  ASSERT_TRUE(await([&] { return !source.isActive(); }, 25000));
  QSignalSpy ready(&controller, &pcm::transcriptionui::CallTranscriptionController::transcriptReady);
  panel->findChild<QPushButton *>("transcriptStop")->click();
  ASSERT_TRUE(await([&] { return ready.count() == 1 && !controller.active(); }));
  EXPECT_EQ(provider->audioSink(), nullptr);
  const auto rows = db->get_transcripts();
  ASSERT_EQ(rows.size(), 1);
  EXPECT_EQ(rows.front().status, "draft");
  EXPECT_EQ(rows.front().consent_scope, "live_local_v1");
  EXPECT_EQ(rows.front().event_id, eventId);
  EXPECT_TRUE(db->get_transcript_client_ids(rows.front().id).empty());
  EXPECT_TRUE(db->get_clients().empty());
  const auto phrases = db->get_transcript_phrases(rows.front().id);
  ASSERT_GE(phrases.size(), 2);
  EXPECT_EQ(panel->phraseCount(), phrases.size());
  for (const auto &role : {"practitioner", "participant"}) {
    EXPECT_TRUE(std::any_of(phrases.begin(), phrases.end(), [=](const auto &phrase) {
      return phrase.track_role == role && !phrase.text.empty() && phrase.end_ms > phrase.start_ms;
    }));
  }
  TranscriptPage review(db, eventId.value_or(0), "Synthetic speech workflow");
  if (standalone) review.reloadTranscript(rows.front().id, "Synthetic speech workflow");
  const auto firstId = phrases.front().id;
  review.findChild<QPushButton *>("editPhrase_" + QString::number(firstId))->click();
  review.findChild<QPlainTextEdit *>("phraseEditor")->setPlainText("Исправленная тестовая реплика");
  review.findChild<QPushButton *>("savePhrase")->click();
  EXPECT_TRUE(db->get_transcript_phrases(rows.front().id).front().edited);
  review.findChild<QPushButton *>("markReviewed")->click();
  EXPECT_EQ(db->get_transcript(rows.front().id)->status, "reviewed");
  review.setConfirmHook([](const QString &) { return true; });
  review.findChild<QPushButton *>("deleteTranscript")->click();
  EXPECT_EQ(db->count_transcripts(), 0);
  EXPECT_TRUE(db->get_transcript_phrases(rows.front().id).empty());
}

TEST(TranscriptionWorkflowModelTest, RealSpeechPersistsBothSpeakersAndCanBeReviewedEditedAndDeleted) {
  runRealSpeechWorkflow(false);
}
TEST(TranscriptionWorkflowModelTest, StandaloneRealSpeechPersistsWithoutEventOrClient) {
  runRealSpeechWorkflow(true);
}

int main(int argc, char **argv) {
  QTemporaryDir home;
  qputenv("HOME", home.path().toUtf8());
  qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
