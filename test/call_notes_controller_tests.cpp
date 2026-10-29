#include "call_notes_controller.h"
#include "call_notes_finalizer.h"
#include <QApplication>
#include <QFile>
#include <QDir>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <gtest/gtest.h>

class CallNotesTest : public ::testing::Test {
protected:
  QTemporaryDir dir;
  std::shared_ptr<pcm::database::Database> db;
  std::unique_ptr<CallNotesController> c;
  qint64 transcript{};
  void SetUp() override {
    pcm::config::Config conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir.path().toStdString())}};
    db = std::make_shared<pcm::database::Database>(conf);
    c = std::make_unique<CallNotesController>(db);
    c->beginCall();
    transcript = db->add_transcript(std::nullopt, "live_local_v1");
    c->attachTranscript(transcript);
  }
  DuckTranscriptPhrase phrase(const std::string &text = "Try a short rehearsal") {
    DuckTranscriptPhrase p; p.transcript_id = transcript; p.track_role = "participant";
    p.text = text; p.start_ms = 1000; p.end_ms = 2000;
    p.id = db->add_transcript_phrase(p); c->addPhrase(p); return p;
  }
  QJsonObject reply(QJsonObject req, const DuckTranscriptPhrase &p) {
    req["notes"] = QJsonObject{{"note", QJsonObject{{"text", "Rehearsal discussed"},
      {"evidence_ids", QJsonArray{QString::number(p.id)}}}}, {"key_points", QJsonArray{}}};
    return req;
  }
};

TEST_F(CallNotesTest, AiSummaryIsStoredWithCallWithoutAcceptStep) {
  ASSERT_TRUE(c->setManualText("My own text"));
  const auto p = phrase(); c->setGenerationEnabled(true);
  QSignalSpy requests(c.get(), &CallNotesController::requestReady); c->flush();
  ASSERT_EQ(requests.count(), 1); c->handleResult(reply(requests[0][0].toJsonObject(), p));
  EXPECT_EQ(c->manualText(), "My own text");
  EXPECT_FALSE(c->draft().contains("accepted"));
  CallNotesController reopened(db); reopened.openTranscript(transcript);
  EXPECT_EQ(reopened.draft()["text"].toString(), "Rehearsal discussed");
  EXPECT_EQ(reopened.manualText(), "My own text");
}
TEST_F(CallNotesTest, SeparateMarkdownEntriesAndAttachmentsSurviveReopeningWithoutAClient) {
  const auto root = dir.path() + "/attachments";
  CallNotesController notes(db, nullptr, root);
  notes.openMaterial(c->materialId());
  QFile file(dir.path() + "/plan.txt");
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write("A short rehearsal plan"); file.close();
  ASSERT_TRUE(notes.setManualText("**My draft**"));
  const auto first = notes.saveNoteEntry("**My draft**", {file.fileName()});
  ASSERT_FALSE(first.isEmpty());
  EXPECT_TRUE(notes.manualText().isEmpty());
  const auto second = notes.saveNoteEntry("- Another note");
  ASSERT_FALSE(second.isEmpty()); EXPECT_NE(first, second);
  CallNotesController reopened(db, nullptr, root); reopened.openMaterial(notes.materialId());
  ASSERT_EQ(reopened.noteEntries().size(), 2);
  const auto entry = reopened.noteEntries()[0].toObject();
  EXPECT_EQ(entry["id"].toString(), first);
  EXPECT_EQ(entry["body_markdown"].toString(), "**My draft**");
  ASSERT_EQ(entry["attachments"].toArray().size(), 1);
  const auto attachment = entry["attachments"].toArray()[0].toObject();
  QFile copy(reopened.noteAttachmentPath(first, attachment["id"].toString()));
  ASSERT_TRUE(copy.open(QIODevice::ReadOnly)); EXPECT_EQ(copy.readAll(), "A short rehearsal plan");
  copy.close();
  ASSERT_TRUE(reopened.saveNoteEntry("Updated", {}, first) == first);
  EXPECT_EQ(reopened.noteEntries().size(), 2);
  EXPECT_EQ(reopened.noteEntries()[0].toObject()["attachments"].toArray().size(), 1);
  EXPECT_TRUE(reopened.noteAttachmentPath(first, "../../outside").isEmpty());
  ASSERT_TRUE(reopened.removeNoteEntry(first));
  EXPECT_EQ(reopened.noteEntries().size(), 1);
  EXPECT_FALSE(QFile::exists(copy.fileName()));
}
TEST_F(CallNotesTest, MissingAttachmentDoesNotPublishPartialEntryOrClearTheDraft) {
  CallNotesController notes(db, nullptr, dir.path() + "/attachments");
  notes.openMaterial(c->materialId());
  ASSERT_TRUE(notes.setManualText("Keep this draft"));
  EXPECT_TRUE(notes.saveNoteEntry("Keep this draft", {dir.path() + "/missing.txt"}).isEmpty());
  EXPECT_TRUE(notes.noteEntries().isEmpty());
  EXPECT_EQ(notes.manualText(), "Keep this draft");
}
TEST_F(CallNotesTest, UnsentAttachmentsAreCopiedAndSurviveClosingAndReopeningTheCall) {
  const auto root = dir.path() + "/attachments";
  CallNotesController notes(db, nullptr, root); notes.openMaterial(c->materialId());
  QFile file(dir.path() + "/unsent.md"); ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write("Keep my attachment"); file.close();
  ASSERT_TRUE(notes.setManualText("Unsent draft"));
  ASSERT_TRUE(notes.setDraftAttachmentPaths({file.fileName()}));
  const auto copied = notes.draftAttachmentPaths(); ASSERT_EQ(copied.size(), 1);
  EXPECT_NE(copied[0], file.fileName());
  ASSERT_TRUE(file.remove());
  CallNotesController reopened(db, nullptr, root); reopened.openMaterial(notes.materialId());
  EXPECT_EQ(reopened.manualText(), "Unsent draft");
  ASSERT_EQ(reopened.draftAttachmentPaths(), copied);
  EXPECT_TRUE(QFile::exists(copied[0]));
  const auto id = reopened.saveNoteEntry("Unsent draft", copied); ASSERT_FALSE(id.isEmpty());
  EXPECT_TRUE(reopened.draftAttachmentPaths().isEmpty());
  EXPECT_FALSE(QFile::exists(copied[0]));
  const auto attachment = reopened.noteEntries()[0].toObject()["attachments"].toArray()[0].toObject();
  EXPECT_TRUE(QFile::exists(reopened.noteAttachmentPath(id, attachment["id"].toString())));
}
TEST_F(CallNotesTest, EditingAnExistingNotePreservesTheUnsentDraftAndItsAttachments) {
  CallNotesController notes(db, nullptr, dir.path() + "/attachments"); notes.openMaterial(c->materialId());
  const auto id = notes.saveNoteEntry("Saved entry"); ASSERT_FALSE(id.isEmpty());
  QFile file(dir.path() + "/draft.txt"); ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write("Draft attachment"); file.close();
  ASSERT_TRUE(notes.setManualText("Unsent draft"));
  ASSERT_TRUE(notes.setDraftAttachmentPaths({file.fileName()}));
  const auto paths = notes.draftAttachmentPaths();
  ASSERT_EQ(notes.saveNoteEntry("Edited entry", {}, id), id);
  EXPECT_EQ(notes.manualText(), "Unsent draft");
  EXPECT_EQ(notes.draftAttachmentPaths(), paths);
  ASSERT_TRUE(notes.setDraftAttachmentPaths({}));
  EXPECT_TRUE(notes.draftAttachmentPaths().isEmpty());
  ASSERT_EQ(paths.size(), 1); EXPECT_FALSE(QFile::exists(paths[0]));
}
TEST_F(CallNotesTest, EntryHistorySurvivesSourceInvalidationAndConcurrentViewSaves) {
  const auto p = phrase();
  const auto first = c->saveNoteEntry("First saved note"); ASSERT_FALSE(first.isEmpty());
  CallNotesController other(db); other.openMaterial(c->materialId());
  ASSERT_FALSE(other.saveNoteEntry("Second saved note").isEmpty());
  ASSERT_TRUE(db->update_transcript_phrase_text(p.id, "Corrected source"));
  ASSERT_EQ(c->saveNoteEntry("First updated", {}, first), first);
  CallNotesController reopened(db); reopened.openMaterial(c->materialId());
  ASSERT_EQ(reopened.noteEntries().size(), 2);
  EXPECT_EQ(reopened.noteEntries()[0].toObject()["body_markdown"].toString(), "First updated");
  EXPECT_EQ(reopened.noteEntries()[1].toObject()["body_markdown"].toString(), "Second saved note");
}
TEST_F(CallNotesTest, EditingAStaleEntryCannotOverwriteChangesFromAnotherView) {
  const auto id = c->saveNoteEntry("Original"); ASSERT_FALSE(id.isEmpty());
  CallNotesController other(db); other.openMaterial(c->materialId());
  ASSERT_EQ(c->saveNoteEntry("First edit", {}, id), id);
  EXPECT_TRUE(other.saveNoteEntry("Stale edit", {}, id).isEmpty());
  CallNotesController reopened(db); reopened.openMaterial(c->materialId());
  EXPECT_EQ(reopened.noteEntries()[0].toObject()["body_markdown"].toString(), "First edit");
}
TEST_F(CallNotesTest, ReplyAfterCancelCannotReplaceDraftOrManualText) {
  c->setManualText("Keep me"); const auto p = phrase(); c->setGenerationEnabled(true);
  QSignalSpy requests(c.get(), &CallNotesController::requestReady); c->flush();
  ASSERT_EQ(requests.count(), 1); const auto out = reply(requests[0][0].toJsonObject(), p);
  c->cancel(); c->handleResult(out);
  EXPECT_TRUE(c->draft().isEmpty()); EXPECT_EQ(c->manualText(), "Keep me");
}
TEST_F(CallNotesTest, EditingEvidenceMakesSummaryStale) {
  const auto p = phrase(); c->setGenerationEnabled(true);
  QSignalSpy requests(c.get(), &CallNotesController::requestReady); c->flush();
  c->handleResult(reply(requests[0][0].toJsonObject(), p));
  ASSERT_TRUE(db->update_transcript_phrase_text(p.id, "Corrected source"));
  EXPECT_FALSE(c->sourcesCurrent());
  EXPECT_EQ(c->finalStatus(), "idle"); EXPECT_FALSE(c->editDraft("Changed"));
}
TEST_F(CallNotesTest, FinalStatusIsIndependentAndFinishesOnlyForMatchingRequest) {
  const auto p = phrase(); c->setGenerationEnabled(true); c->finishCall();
  QSignalSpy requests(c.get(), &CallNotesController::requestReady); c->flush();
  ASSERT_EQ(requests.count(), 1); EXPECT_EQ(c->finalStatus(), "running");
  c->handleResult(reply(requests[0][0].toJsonObject(), p));
  EXPECT_EQ(c->finalStatus(), "ready"); EXPECT_TRUE(c->ended());
}
TEST_F(CallNotesTest, NoTranscriptStillSavesManualNotesForTheCall) {
  CallNotesController plain(db); plain.beginCall();
  ASSERT_GT(plain.materialId(), 0); ASSERT_TRUE(plain.setManualText("No speech recognition"));
  EXPECT_FALSE(plain.materialId() == c->materialId());
}
TEST_F(CallNotesTest, TwoViewsCannotAcceptEachOthersOldReplyAfterSourceCorrection) {
  const auto p = phrase(); c->setGenerationEnabled(true);
  QSignalSpy first(c.get(), &CallNotesController::requestReady); c->flush();
  ASSERT_EQ(first.count(), 1);
  const auto stale = reply(first[0][0].toJsonObject(), p);
  ASSERT_TRUE(db->update_transcript_phrase_text(p.id, "I declined the rehearsal"));
  CallNotesController other(db); other.openTranscript(transcript); other.setGenerationEnabled(true);
  QSignalSpy second(&other, &CallNotesController::requestReady); other.finishCall(); other.flush();
  ASSERT_EQ(second.count(), 1);
  other.handleResult(stale);
  EXPECT_TRUE(other.draft().isEmpty()); EXPECT_TRUE(other.busy());
}
TEST_F(CallNotesTest, FinalCanBeRetriedAfterEditingAnExistingSourceWithoutReopening) {
  const auto p = phrase(); c->setGenerationEnabled(true); c->finishCall();
  QSignalSpy requests(c.get(), &CallNotesController::requestReady); c->flush();
  ASSERT_EQ(requests.count(), 1); c->handleResult(reply(requests[0][0].toJsonObject(), p));
  ASSERT_TRUE(db->update_transcript_phrase_text(p.id, "Corrected source"));
  c->requestFinal(); ASSERT_EQ(requests.count(), 2); EXPECT_TRUE(c->busy());
  EXPECT_EQ(requests[1][0].toJsonObject()["utterances"].toArray()[0].toObject()["text"].toString(), "Corrected source");
}
TEST_F(CallNotesTest, AcceptedAgreementDoesNotChangeMyNotes) {
  ASSERT_TRUE(c->setManualText("Mine"));
  ASSERT_TRUE(c->appendAgreement("a1", "Send documents"));
  ASSERT_TRUE(c->appendAgreement("a1", "Send documents"));
  EXPECT_EQ(c->manualText(), "Mine");
}
TEST_F(CallNotesTest, FinalSummaryStartsAutomaticallyWhenCallEnds) {
  phrase(); c->setGenerationEnabled(true);
  QSignalSpy requests(c.get(), &CallNotesController::requestReady);
  c->finishCall(); c->flush();
  ASSERT_EQ(requests.count(), 1);
  EXPECT_TRUE(requests[0][0].toJsonObject()["final"].toBool());
}
TEST_F(CallNotesTest, BackgroundFinalCanWaitForAnActiveCallAndBeCancelled) {
  phrase(); c->finishCall();
  const auto connection = QObject::connect(c.get(), &CallNotesController::generationRequested,
                                          c.get(), &CallNotesController::deferFinal);
  QSignalSpy requests(c.get(), &CallNotesController::requestReady);
  c->requestFinal(); EXPECT_EQ(c->finalStatus(), "queued"); EXPECT_TRUE(requests.isEmpty());
  c->cancel(); EXPECT_EQ(c->finalStatus(), "cancelled");
  QObject::disconnect(connection);
  c->setGenerationEnabled(true); c->requestFinal();
  EXPECT_EQ(c->finalStatus(), "running"); EXPECT_EQ(requests.count(), 1);
}
TEST_F(CallNotesTest, FinalMergesWithManualTextEditedInAnotherView) {
  const auto p = phrase(); c->setGenerationEnabled(true); c->finishCall();
  QSignalSpy requests(c.get(), &CallNotesController::requestReady); c->flush();
  ASSERT_EQ(requests.count(), 1);
  CallNotesController editor(db); editor.openMaterial(c->materialId());
  ASSERT_TRUE(editor.setManualText("Written while the final was running"));
  c->handleResult(reply(requests[0][0].toJsonObject(), p));
  CallNotesController reopened(db); reopened.openMaterial(c->materialId());
  EXPECT_EQ(reopened.manualText(), "Written while the final was running");
  EXPECT_EQ(reopened.finalStatus(), "ready");
  EXPECT_EQ(reopened.draft()["text"].toString(), "Rehearsal discussed");
}
TEST_F(CallNotesTest, PreviousCallWaitsForItsWriterAndYieldsToTheNextCall) {
  const auto p = phrase(); const auto first = c->materialId();
  c->finishCall();
  CallNotesFinalizer finals(db);
  QObject::connect(&finals, &CallNotesFinalizer::generationRequested, &finals,
                   [&] { finals.setGenerationEnabled(true); });
  QSignalSpy requests(&finals, &CallNotesFinalizer::requestReady);
  finals.enqueue(first, transcript);
  finals.setCallActive(true); c->beginCall(); c->setManualText("Second call");
  const auto second = c->materialId();
  finals.transcriptReady(transcript);
  QCoreApplication::processEvents(); EXPECT_TRUE(requests.isEmpty());
  finals.setCallActive(false); QCoreApplication::processEvents();
  ASSERT_EQ(requests.count(), 1);
  EXPECT_EQ(requests[0][0].toJsonObject()["material_id"].toInteger(), first);
  finals.handleResult(reply(requests[0][0].toJsonObject(), p));
  CallNotesController reopened(db); reopened.openMaterial(first);
  EXPECT_EQ(reopened.finalStatus(), "ready");
  reopened.openMaterial(second); EXPECT_EQ(reopened.manualText(), "Second call");
  EXPECT_TRUE(reopened.draft().isEmpty());
}
TEST_F(CallNotesTest, FinalWaitsForTheMatchingTranscriptEvenWithoutAnotherCall) {
  const auto p = phrase(); c->finishCall();
  CallNotesFinalizer finals(db);
  QObject::connect(&finals, &CallNotesFinalizer::generationRequested, &finals,
                   [&] { finals.setGenerationEnabled(true); });
  QSignalSpy requests(&finals, &CallNotesFinalizer::requestReady);
  finals.enqueue(c->materialId(), transcript); QCoreApplication::processEvents();
  EXPECT_TRUE(requests.isEmpty());
  finals.transcriptReady(transcript + 100); QCoreApplication::processEvents();
  EXPECT_TRUE(requests.isEmpty());
  finals.transcriptReady(transcript); QCoreApplication::processEvents();
  ASSERT_EQ(requests.count(), 1);
  finals.handleResult(reply(requests[0][0].toJsonObject(), p));
  CallNotesController reopened(db); reopened.openMaterial(c->materialId());
  EXPECT_EQ(reopened.finalStatus(), "ready");
}
TEST_F(CallNotesTest, InterruptedFinalIsRetriedAndRejectsItsOldResponse) {
  const auto p = phrase(); c->finishCall();
  CallNotesFinalizer finals(db);
  QObject::connect(&finals, &CallNotesFinalizer::generationRequested, &finals,
                   [&] { finals.setGenerationEnabled(true); });
  QSignalSpy requests(&finals, &CallNotesFinalizer::requestReady);
  QSignalSpy cancels(&finals, &CallNotesFinalizer::cancelRequested);
  finals.enqueue(c->materialId()); QCoreApplication::processEvents();
  ASSERT_EQ(requests.count(), 1); const auto old = requests[0][0].toJsonObject();
  finals.setCallActive(true); EXPECT_EQ(cancels.count(), 1);
  finals.handleResult(reply(old, p));
  finals.setCallActive(false); QCoreApplication::processEvents();
  ASSERT_EQ(requests.count(), 2);
  EXPECT_NE(old["request_token"], requests[1][0].toJsonObject()["request_token"]);
  finals.handleResult(reply(old, p));
  finals.handleResult(reply(requests[1][0].toJsonObject(), p));
  CallNotesController reopened(db); reopened.openMaterial(c->materialId());
  EXPECT_EQ(reopened.finalStatus(), "ready");
}
TEST_F(CallNotesTest, CancelledQueuedFinalNeverRestartsWhenAnotherCallEnds) {
  phrase(); c->finishCall();
  CallNotesFinalizer finals(db); finals.setCallActive(true);
  QObject::connect(&finals, &CallNotesFinalizer::generationRequested, &finals,
                   [&] { finals.setGenerationEnabled(true); });
  QSignalSpy requests(&finals, &CallNotesFinalizer::requestReady);
  finals.enqueue(c->materialId(), transcript); finals.cancel(c->materialId());
  finals.transcriptReady(transcript); finals.setCallActive(false);
  QCoreApplication::processEvents(); EXPECT_TRUE(requests.isEmpty());
  CallNotesController reopened(db); reopened.openMaterial(c->materialId());
  EXPECT_EQ(reopened.finalStatus(), "cancelled");
}
TEST_F(CallNotesTest, SavedViewCanFollowAndCancelAnAutomaticFinalQueuedBehindAnotherCall) {
  phrase(); c->finishCall();
  CallNotesFinalizer finals(db); finals.setCallActive(true);
  QObject::connect(&finals, &CallNotesFinalizer::generationRequested, &finals,
                   [&] { finals.setGenerationEnabled(true); });
  QSignalSpy requests(&finals, &CallNotesFinalizer::requestReady);
  finals.enqueue(c->materialId());
  CallNotesController saved(db); saved.openMaterial(c->materialId());
  saved.refreshFromStorage(); EXPECT_EQ(saved.finalStatus(), "queued");
  QObject::connect(&saved, &CallNotesController::cancelRequested, &finals,
                   [&](const QString &) { finals.cancel(saved.materialId()); });
  saved.cancel(); finals.setCallActive(false); QCoreApplication::processEvents();
  EXPECT_TRUE(requests.isEmpty());
  CallNotesController reopened(db); reopened.openMaterial(c->materialId());
  EXPECT_EQ(reopened.finalStatus(), "cancelled");
}
TEST_F(CallNotesTest, FailedLiveUpdateRetriesByItselfWithGrowingDelay) {
  c->setRetryDelay(1000);
  const auto p = phrase(); c->setGenerationEnabled(true);
  QSignalSpy requests(c.get(), &CallNotesController::requestReady); c->flush();
  ASSERT_EQ(requests.count(), 1);
  c->handleResult(reply(requests.last()[0].toJsonObject(), p));
  ASSERT_EQ(c->liveStatus(), "ready");
  for (const auto *reason : {"invalid_output", "runtime", "timeout"}) {
    const auto before = requests.count();
    phrase(); c->flush();
    ASSERT_EQ(requests.count(), before + 1);
    auto failed = requests.last()[0].toJsonObject();
    failed["error"] = "inference_failed"; failed["reason"] = reason;
    c->handleResult(failed);
    // No Retry button: the last good summary stays while an update is queued.
    EXPECT_EQ(c->liveStatus(), "queued") << reason;
    EXPECT_TRUE(c->retryPending());
    EXPECT_EQ(c->draft()["text"].toString(), "Rehearsal discussed");
    EXPECT_FALSE(c->busy());
  }
  // The third failure in a row waits 1000 * 2^2 ms. A new phrase does not
  // cut that backoff short.
  const auto before = requests.count();
  phrase();
  QTest::qWait(2500);
  EXPECT_EQ(requests.count(), before);
  ASSERT_TRUE(requests.wait(3000));
  const auto retried = requests.last()[0].toJsonObject();
  EXPECT_EQ(retried["final"].toBool(), false);
  c->handleResult(reply(retried, p));
  EXPECT_EQ(c->liveStatus(), "ready");
  EXPECT_FALSE(c->retryPending());
}
TEST_F(CallNotesTest, BrokenWorkerOrFinalNoteStillWaitForTheUser) {
  c->setRetryDelay(10);
  phrase(); c->setGenerationEnabled(true);
  QSignalSpy requests(c.get(), &CallNotesController::requestReady); c->flush();
  ASSERT_EQ(requests.count(), 1);
  c->workerFailed();
  EXPECT_EQ(c->liveStatus(), "error");
  EXPECT_FALSE(c->retryPending());
  c->setGenerationEnabled(true); c->finishCall(); c->flush();
  ASSERT_EQ(requests.count(), 2);
  auto failed = requests.last()[0].toJsonObject();
  ASSERT_TRUE(failed["final"].toBool());
  failed["error"] = "inference_failed"; failed["reason"] = "runtime";
  c->handleResult(failed);
  EXPECT_EQ(c->finalStatus(), "error");
  EXPECT_FALSE(c->retryPending());
  QTest::qWait(50);
  EXPECT_EQ(requests.count(), 2);
}
int main(int argc, char **argv) {
  QApplication app(argc, argv); ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS();
}
