#include "call_prompter_controller.h"
#include "config.h"
#include <QApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

class PrompterControllerTest : public ::testing::Test {
protected:
  void SetUp() override {
    pcm::config::Config conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(temp.path().toStdString())}};
    db = std::make_shared<pcm::database::Database>(conf);
    id = db->add_transcript(std::nullopt, "live_local_v1");
    controller = std::make_unique<CallPrompterController>(db);
    controller->beginTranscript(id);
    controller->resume();
  }
  DuckTranscriptPhrase phrase(const std::string &text) {
    DuckTranscriptPhrase p;
    p.transcript_id = id; p.text = text; p.speaker_name = "Speaker";
    p.track_role = "participant"; p.start_ms = 1000; p.end_ms = 2000;
    p.id = db->add_transcript_phrase(p);
    return p;
  }
  QJsonObject reply(const QJsonObject &request, const DuckTranscriptPhrase &p, const QString &target = {}) {
    auto out = request;
    out.remove("utterances"); out.remove("candidates");
    out["events"] = QJsonArray{QJsonObject{{"kind", "accept"}, {"action", "Send document Friday"},
      {"participants", QJsonArray{"Speaker"}}, {"evidence_ids", QJsonArray{QString::number(p.id)}},
      {"target_id", target}, {"unresolved", false}}};
    out["suggestions"] = QJsonArray{}; out["rejected"] = QJsonArray{};
    return out;
  }
  QTemporaryDir temp;
  std::shared_ptr<pcm::database::Database> db;
  qint64 id{};
  std::unique_ptr<CallPrompterController> controller;
};

TEST_F(PrompterControllerTest, OnlyOneRequestInFlightAndRepliesDoNotAcceptCards) {
  QSignalSpy requests(controller.get(), &CallPrompterController::requestReady);
  const auto p = phrase("Send document Friday");
  controller->addPhrase(p); controller->addPhrase(p); controller->flush();
  ASSERT_EQ(requests.count(), 1);
  const auto next = phrase("Agreed");
  controller->addPhrase(next); controller->flush();
  EXPECT_EQ(requests.count(), 1);
  const auto request = requests[0][0].toJsonObject();
  controller->handleResult(reply(request, p));
  ASSERT_EQ(controller->cards().size(), 1);
  EXPECT_EQ(controller->cards()[0].toObject()["decision"].toString(), "pending");
  EXPECT_TRUE(controller->records().isEmpty());
  controller->flush();
  EXPECT_EQ(requests.count(), 2);
}
TEST_F(PrompterControllerTest, EventsSupportedOnlyByContextPhrasesAreNotProposedAgain) {
  QSignalSpy requests(controller.get(), &CallPrompterController::requestReady);
  const auto p = phrase("Send document Friday");
  controller->addPhrase(p); controller->flush();
  controller->handleResult(reply(requests[0][0].toJsonObject(), p));
  ASSERT_EQ(controller->cards().size(), 1);
  const auto q = phrase("Fine");
  controller->addPhrase(q); controller->flush();
  ASSERT_EQ(requests.count(), 2);
  const auto request = requests[1][0].toJsonObject();
  ASSERT_EQ(request["utterances"].toArray().size(), 2); // p is context only
  // The model restates the already processed agreement in other words.
  auto restated = reply(request, p);
  auto event = restated["events"][0].toObject();
  event["action"] = "Send the document on Friday";
  restated["events"] = QJsonArray{event};
  controller->handleResult(restated);
  EXPECT_EQ(controller->cards().size(), 1);
}
TEST_F(PrompterControllerTest, EventsCitingANewPhraseAlongsideContextAreProposed) {
  QSignalSpy requests(controller.get(), &CallPrompterController::requestReady);
  const auto p = phrase("Send document Friday");
  controller->addPhrase(p); controller->flush();
  controller->handleResult(reply(requests[0][0].toJsonObject(), p));
  const auto q = phrase("Yes, Monday instead");
  controller->addPhrase(q); controller->flush();
  auto moved = reply(requests[1][0].toJsonObject(), p);
  auto event = moved["events"][0].toObject();
  event["action"] = "Send document Monday";
  event["evidence_ids"] = QJsonArray{QString::number(p.id), QString::number(q.id)};
  moved["events"] = QJsonArray{event};
  controller->handleResult(moved);
  EXPECT_EQ(controller->cards().size(), 2);
}
TEST_F(PrompterControllerTest, LateReplyAfterEndCannotChangeStoredCards) {
  QSignalSpy requests(controller.get(), &CallPrompterController::requestReady);
  const auto p = phrase("Send document Friday");
  controller->addPhrase(p); controller->flush();
  const auto request = requests[0][0].toJsonObject();
  controller->endCall(); controller->handleResult(reply(request, p));
  EXPECT_TRUE(controller->cards().isEmpty());
}
TEST_F(PrompterControllerTest, ForeignTargetOrEvidenceFailsClosed) {
  QSignalSpy requests(controller.get(), &CallPrompterController::requestReady);
  const auto p = phrase("Send document Friday");
  controller->addPhrase(p); controller->flush();
  auto result = reply(requests[0][0].toJsonObject(), p, "foreign");
  controller->handleResult(result);
  EXPECT_TRUE(controller->cards().isEmpty());
  EXPECT_TRUE(controller->records().isEmpty());
}
TEST_F(PrompterControllerTest, AcceptAndRejectPersistAndDoNotOverwriteManualDecisions) {
  QSignalSpy requests(controller.get(), &CallPrompterController::requestReady);
  const auto p = phrase("Send document Friday");
  controller->addPhrase(p); controller->flush();
  controller->handleResult(reply(requests[0][0].toJsonObject(), p));
  const auto cardId = controller->cards()[0].toObject()["id"].toString();
  ASSERT_TRUE(controller->acceptCard(cardId, "Send document Monday"));
  EXPECT_EQ(controller->records()[0].toObject()["text"].toString(), "Send document Monday");
  CallPrompterController reopened(db);
  reopened.beginTranscript(id);
  ASSERT_EQ(reopened.records().size(), 1);
  EXPECT_EQ(reopened.records()[0].toObject()["text"].toString(), "Send document Monday");
}
TEST_F(PrompterControllerTest, PendingAgreementCanBeRescheduledBeforeReview) {
  QSignalSpy requests(controller.get(), &CallPrompterController::requestReady);
  const auto p = phrase("Send document Friday");
  controller->addPhrase(p); controller->flush();
  controller->handleResult(reply(requests[0][0].toJsonObject(), p));
  const auto original = controller->cards()[0].toObject()["id"].toString();
  const auto change = phrase("Actually Monday");
  controller->addPhrase(change); controller->flush();
  const auto request = requests[1][0].toJsonObject();
  ASSERT_EQ(request["candidates"].toArray().size(), 1);
  auto revised = reply(request, change, original);
  auto event = revised["events"].toArray()[0].toObject(); event["action"] = "Send document Monday";
  revised["events"] = QJsonArray{event}; controller->handleResult(revised);
  ASSERT_EQ(controller->cards().size(), 2);
  ASSERT_TRUE(controller->acceptCard(controller->cards()[1].toObject()["id"].toString()));
  ASSERT_EQ(controller->records().size(), 1);
  EXPECT_EQ(controller->records()[0].toObject()["text"].toString(), "Send document Monday");
  EXPECT_EQ(controller->cards()[0].toObject()["decision"].toString(), "superseded");
}
TEST_F(PrompterControllerTest, UnmatchedCancellationRequiresExplicitTargetSelection) {
  QSignalSpy requests(controller.get(), &CallPrompterController::requestReady);
  const auto p = phrase("Send document Friday");
  controller->addPhrase(p); controller->flush(); controller->handleResult(reply(requests[0][0].toJsonObject(), p));
  const auto original = controller->cards()[0].toObject()["id"].toString();
  ASSERT_TRUE(controller->acceptCard(original));
  const auto cancel = phrase("Cancel delivery"); controller->addPhrase(cancel); controller->flush();
  auto response = reply(requests[1][0].toJsonObject(), cancel);
  auto event = response["events"].toArray()[0].toObject(); event["kind"] = "cancel"; event["unresolved"] = true;
  response["events"] = QJsonArray{event}; controller->handleResult(response);
  const auto cancelId = controller->cards()[1].toObject()["id"].toString();
  EXPECT_FALSE(controller->acceptCard(cancelId));
  EXPECT_FALSE(controller->resolveCard(cancelId, "foreign"));
  ASSERT_TRUE(controller->resolveCard(cancelId, original));
  EXPECT_TRUE(controller->records()[0].toObject()["active"].toBool());
  ASSERT_TRUE(controller->acceptCard(cancelId));
  EXPECT_FALSE(controller->records()[0].toObject()["active"].toBool());
}
TEST_F(PrompterControllerTest, ManualDecisionMakesInflightReplyStaleAndRequeuesItsPhrases) {
  QSignalSpy requests(controller.get(), &CallPrompterController::requestReady);
  const auto first = phrase("Send document Friday");
  controller->addPhrase(first); controller->flush(); controller->handleResult(reply(requests[0][0].toJsonObject(), first));
  const auto id = controller->cards()[0].toObject()["id"].toString();
  const auto second = phrase("Another agreement"); controller->addPhrase(second); controller->flush();
  const auto staleRequest = requests[1][0].toJsonObject();
  ASSERT_TRUE(controller->acceptCard(id, "Manual correction"));
  controller->handleResult(reply(staleRequest, second));
  EXPECT_EQ(controller->cards().size(), 1);
  EXPECT_EQ(controller->records()[0].toObject()["text"].toString(), "Manual correction");
  controller->flush();
  ASSERT_EQ(requests.count(), 3);
  const auto fresh = requests[2][0].toJsonObject();
  EXPECT_NE(fresh["session_generation"], staleRequest["session_generation"]);
  EXPECT_GT(fresh["ledger_revision"].toInteger(), staleRequest["ledger_revision"].toInteger());
  EXPECT_EQ(fresh["utterances"].toArray().last().toObject()["id"].toString(), QString::number(second.id));
}
TEST_F(PrompterControllerTest, RepeatedEvidenceDoesNotUndoManualCorrection) {
  QSignalSpy requests(controller.get(), &CallPrompterController::requestReady);
  const auto p = phrase("Send document Friday");
  controller->addPhrase(p); controller->flush();
  controller->handleResult(reply(requests[0][0].toJsonObject(), p));
  ASSERT_TRUE(controller->acceptCard(controller->cards()[0].toObject()["id"].toString(), "Manual correction"));
  const auto neutral = phrase("Thanks"); controller->addPhrase(neutral); controller->flush();
  controller->handleResult(reply(requests[1][0].toJsonObject(), p));
  EXPECT_EQ(controller->cards().size(), 1);
  EXPECT_EQ(controller->records()[0].toObject()["text"].toString(), "Manual correction");
}
TEST_F(PrompterControllerTest, EditedEvidenceCannotBeAcceptedOrSavedAsTheOldProposal) {
  QSignalSpy requests(controller.get(), &CallPrompterController::requestReady);
  const auto p = phrase("Send document Friday"); controller->addPhrase(p); controller->flush();
  controller->handleResult(reply(requests[0][0].toJsonObject(), p));
  const auto cardId = controller->cards()[0].toObject()["id"].toString();
  ASSERT_TRUE(db->update_transcript_phrase_text(p.id, "No promise was made"));
  EXPECT_FALSE(controller->acceptCard(cardId));
  EXPECT_TRUE(controller->records().isEmpty());
}
TEST_F(PrompterControllerTest, LookbackDoesNotBlockAValidNewPhrase) {
  QSignalSpy requests(controller.get(), &CallPrompterController::requestReady);
  const auto a = phrase(std::string(5000, 'a')), b = phrase(std::string(5000, 'b'));
  controller->addPhrase(a); controller->addPhrase(b); controller->flush();
  auto empty = requests[0][0].toJsonObject(); empty["events"] = QJsonArray{}; empty["suggestions"] = QJsonArray{};
  controller->handleResult(empty);
  const auto c = phrase(std::string(2000, 'c')); controller->addPhrase(c); controller->flush();
  ASSERT_EQ(requests.count(), 2);
  EXPECT_EQ(requests[1][0].toJsonObject()["utterances"].toArray().last().toObject()["id"].toString(), QString::number(c.id));
}
TEST_F(PrompterControllerTest, FailedBatchIsRetriedInHalvesThenSkippedWithoutPausing) {
  QSignalSpy requests(controller.get(), &CallPrompterController::requestReady);
  std::vector<DuckTranscriptPhrase> phrases;
  for (int i = 0; i < 4; ++i) { phrases.push_back(phrase("Phrase " + std::to_string(i))); controller->addPhrase(phrases.back()); }
  controller->flush();
  const auto failWith = [&](int index) {
    auto failed = requests[index][0].toJsonObject();
    failed.remove("utterances"); failed.remove("candidates");
    failed["error"] = "inference_failed"; failed["reason"] = "invalid_output";
    controller->handleResult(failed);
    controller->flush();
  };
  ASSERT_EQ(requests[0][0].toJsonObject()["utterances"].toArray().size(), 4);
  failWith(0);
  ASSERT_EQ(requests.count(), 2);
  EXPECT_EQ(requests[1][0].toJsonObject()["utterances"].toArray().size(), 2);
  failWith(1);
  ASSERT_EQ(requests.count(), 3);
  EXPECT_EQ(requests[2][0].toJsonObject()["utterances"].toArray().size(), 1);
  failWith(2); // a lone phrase without lookback is skipped
  ASSERT_EQ(requests.count(), 4);
  const auto next = requests[3][0].toJsonObject()["utterances"].toArray();
  ASSERT_EQ(next.size(), 1);
  EXPECT_EQ(next[0].toObject()["id"].toString(), QString::number(phrases[1].id));
  EXPECT_TRUE(controller->running());
  auto empty = requests[3][0].toJsonObject(); empty["events"] = QJsonArray{}; empty["suggestions"] = QJsonArray{};
  controller->handleResult(empty);
  controller->flush();
  ASSERT_EQ(requests.count(), 5);
  EXPECT_EQ(requests[4][0].toJsonObject()["utterances"].toArray().size(), 3); // phrase 1 as lookback, then 2 and 3
}
TEST_F(PrompterControllerTest, RepeatedFailuresPauseThePrompter) {
  QSignalSpy requests(controller.get(), &CallPrompterController::requestReady);
  for (int i = 0; i < 10; ++i) controller->addPhrase(phrase("Phrase " + std::to_string(i)));
  for (int i = 0; i < 8 && controller->running(); ++i) {
    controller->flush();
    auto failed = requests.last()[0].toJsonObject();
    failed["error"] = "inference_failed"; failed["reason"] = "runtime";
    controller->handleResult(failed);
  }
  EXPECT_FALSE(controller->running());
}
TEST_F(PrompterControllerTest, DeletedTranscriptClearsAllVisibleDerivedData) {
  QSignalSpy requests(controller.get(), &CallPrompterController::requestReady);
  const auto p = phrase("Send document Friday"); controller->addPhrase(p); controller->flush();
  controller->handleResult(reply(requests[0][0].toJsonObject(), p));
  ASSERT_FALSE(controller->cards().isEmpty());
  ASSERT_TRUE(db->delete_transcript(id));
  controller->transcriptDeleted(id);
  EXPECT_TRUE(controller->cards().isEmpty());
  EXPECT_TRUE(controller->records().isEmpty());
  EXPECT_TRUE(controller->suggestions().isEmpty());
  EXPECT_TRUE(controller->source(QString::number(p.id)).isEmpty());
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
