#include "prompter_pipeline.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <deque>
#include <gtest/gtest.h>

using namespace pcm::prompter;

namespace {

QJsonValue json(const char *text) {
  return QJsonDocument::fromJson(QByteArray("[") + text + "]").array().first();
}

class PipelineTest : public ::testing::Test {
protected:
  QJsonObject request{
      {"protocol_version", 1}, {"request_id", 7}, {"session_generation", 3}, {"ledger_revision", 4},
      {"utterances", json(R"([{"id":"1","speaker":"Анна","text":"Отправлю документ в пятницу."},
                              {"id":"2","speaker":"Борис","text":"Хорошо, жду в пятницу."}])")},
      {"candidates", QJsonArray{}}};
  std::vector<QByteArray> payloads;

  QJsonObject run(std::deque<QJsonValue> replies) {
    return processRequest(request, [&](const QString &, const QByteArray &payload) {
      payloads.push_back(payload);
      if (replies.empty()) throw PipelineError("unexpected model call");
      auto reply = replies.front();
      replies.pop_front();
      return reply;
    });
  }

  static QJsonObject event(QJsonObject changes = {}) {
    QJsonObject value{{"kind", "accept"}, {"action", "Анна отправит документ в пятницу"},
                      {"participants", QJsonArray{"Анна", "Борис"}}, {"evidence_ids", QJsonArray{"1", "2"}}};
    for (auto it = changes.begin(); it != changes.end(); ++it) value.insert(it.key(), it.value());
    return value;
  }
  static QJsonObject events(const QJsonObject &e) { return {{"events", QJsonArray{e}}}; }
};

} // namespace

TEST_F(PipelineTest, EventsRemainProposalsAndReplyEchoesRevision) {
  const auto out = run({events(event()), QJsonObject{{"suggestions", QJsonArray{}}}});
  EXPECT_EQ(out["request_id"].toInt(), 7);
  EXPECT_EQ(out["session_generation"].toInt(), 3);
  EXPECT_EQ(out["ledger_revision"].toInt(), 4);
  EXPECT_EQ(out["events"][0]["target_id"].toString(), "");
}

TEST_F(PipelineTest, ForeignEvidenceAndSpeakerAreRejected) {
  for (const auto &bad : {event({{"evidence_ids", QJsonArray{"foreign"}}}), event({{"participants", QJsonArray{"Unknown"}}})}) {
    const auto out = run({events(bad), QJsonObject{}});
    EXPECT_TRUE(out["events"].toArray().isEmpty());
    EXPECT_FALSE(out["rejected"].toArray().isEmpty());
  }
}

TEST_F(PipelineTest, AmbiguousCancellationRemainsUnresolved) {
  request["candidates"] = json(R"([{"id":"a","text":"Документ в пятницу","participants":["Анна","Борис"]},
                                   {"id":"b","text":"Документ в субботу","participants":["Анна","Борис"]}])");
  const auto out = run({events(event({{"kind", "cancel"}})), json(R"({"matches":["a","b"]})"), QJsonObject{}});
  EXPECT_TRUE(out["events"][0]["unresolved"].toBool());
  EXPECT_EQ(out["events"][0]["target_id"].toString(), "");
}

TEST_F(PipelineTest, ForeignMatchingTargetCannotBeApplied) {
  request["candidates"] = json(R"([{"id":"a","text":"Документ","participants":["Анна"]}])");
  const auto out = run({events(event({{"kind", "cancel"}})), json(R"({"matches":["foreign"]})"), QJsonObject{}});
  EXPECT_TRUE(out["events"][0]["unresolved"].toBool());
}

TEST_F(PipelineTest, ExactMatchAvoidsSecondModelCall) {
  request["candidates"] = json(R"([{"id":"a","text":"анна отправит документ   в пятницу.","participants":["Анна","Борис"]}])");
  const auto out = run({events(event({{"kind", "cancel"}}))});
  EXPECT_EQ(out["events"][0]["target_id"].toString(), "a");
  EXPECT_EQ(payloads.size(), 1u);
}

TEST_F(PipelineTest, RescheduleReturnedAsCancelAndAcceptBecomesOneChange) {
  request["candidates"] = json(R"([{"id":"meeting","text":"Встреча завтра в 15:00","participants":["Анна","Борис"]}])");
  const auto cancel = event({{"kind", "cancel"}, {"action", "Встреча завтра в 15:00"}});
  const auto moved = event({{"action", "Встреча в четверг в 16:00"}});
  // Exact match resolves the cancellation; the new time needs the matcher,
  // which (as in the live call) does not recognise it as the same meeting.
  const auto out = run({QJsonObject{{"events", QJsonArray{cancel, moved}}}, json(R"({"matches":[]})"), QJsonObject{}});
  ASSERT_EQ(out["events"].toArray().size(), 1);
  const auto change = out["events"][0].toObject();
  EXPECT_EQ(change["kind"].toString(), "accept");
  EXPECT_EQ(change["action"].toString(), "Встреча в четверг в 16:00");
  EXPECT_EQ(change["target_id"].toString(), "meeting");
  EXPECT_FALSE(change["unresolved"].toBool());
}

TEST_F(PipelineTest, CancellationAndUnrelatedAgreementStaySeparate) {
  request["utterances"] = json(R"([{"id":"1","speaker":"Анна","text":"Встречу завтра отменяем."},
                                   {"id":"2","speaker":"Борис","text":"Хорошо."},
                                   {"id":"3","speaker":"Анна","text":"А документы пришлю в пятницу."}])");
  request["candidates"] = json(R"([{"id":"meeting","text":"Встреча завтра в 15:00","participants":["Анна","Борис"]}])");
  const auto cancel = event({{"kind", "cancel"}, {"action", "Встреча завтра в 15:00"}, {"evidence_ids", QJsonArray{"1", "2"}}});
  const auto docs = event({{"action", "Анна пришлёт документы в пятницу"}, {"evidence_ids", QJsonArray{"3"}}});
  const auto out = run({QJsonObject{{"events", QJsonArray{cancel, docs}}}, json(R"({"matches":[]})"), QJsonObject{}});
  EXPECT_EQ(out["events"].toArray().size(), 2);
}

TEST_F(PipelineTest, NeutralWindowSkipsTheDeadlineModelCall) {
  const auto out = run({json(R"({"events":[]})")});
  EXPECT_TRUE(out["suggestions"].toArray().isEmpty());
  EXPECT_EQ(payloads.size(), 1u);
}

TEST_F(PipelineTest, DeadlineCheckCreatesAFixedQuestionWithSourceIds) {
  const auto out = run({events(event()), json(R"({"missing_deadline":true,"evidence_ids":["1","2"]})")});
  ASSERT_EQ(out["suggestions"].toArray().size(), 1);
  EXPECT_EQ(out["suggestions"][0]["evidence_ids"], json(R"(["1","2"])"));
  EXPECT_EQ(out["suggestions"][0]["text"].toString(), "К какому сроку вы договорились это выполнить?");
}

TEST_F(PipelineTest, StatedDeadlineOrForeignSourceDoesNotCreateAQuestion) {
  for (const char *hint : {R"({"missing_deadline":false,"evidence_ids":["1"]})",
                           R"({"missing_deadline":true,"evidence_ids":["foreign"]})",
                           R"({"missing_deadline":"true","evidence_ids":["1"]})"}) {
    const auto out = run({events(event()), json(hint)});
    EXPECT_TRUE(out["suggestions"].toArray().isEmpty()) << hint;
  }
}

TEST_F(PipelineTest, MissingProtocolFieldsAndExcessInputFailClosed) {
  for (const char *field : {"request_id", "ledger_revision", "session_generation"}) {
    auto broken = request;
    broken.remove(field);
    EXPECT_THROW(processRequest(broken, [](const QString &, const QByteArray &) { return QJsonValue(); }), PipelineError);
  }
  request["utterances"] = QJsonArray{QJsonObject{{"id", "1"}, {"speaker", "A"}, {"text", QString(20000, 'x')}}};
  EXPECT_THROW(run({}), PipelineError);
}

TEST_F(PipelineTest, InvalidExtractionFailsInsteadOfReportingProcessed) {
  EXPECT_THROW(run({json(R"({"events":"not an array"})")}), PipelineError);
}

TEST_F(PipelineTest, ModelInputMatchesTheValidatedPythonSerialisation) {
  request["candidates"] = json(R"([{"id":"r1","participants":["Анна"],"text":"Документ"}])");
  const auto extracted = json(R"({"events":[{"kind":"accept","participants":["Анна"],"action":"Документ \"А\"","evidence_ids":["1"]}]})");
  run({extracted, json(R"({"matches":[]})"), QJsonObject{}});
  // Expected bytes are what worker.py's json.dumps(payload, ensure_ascii=False) produced.
  const QByteArray window = R"("[1] Анна: Отправлю документ в пятницу.\n[2] Борис: Хорошо, жду в пятницу.")";
  const QByteArray eventJson = R"({"kind": "accept", "participants": ["Анна"], "action": "Документ \"А\"", "evidence_ids": ["1"])";
  ASSERT_EQ(payloads.size(), 3u);
  EXPECT_EQ(payloads[0], "{\"new_utterances\": " + window + "}");
  EXPECT_EQ(payloads[1], "{\"event\": " + eventJson + R"(}, "records": [{"id": "r1", "participants": ["Анна"], "text": "Документ"}], "new_utterances": )" + window + "}");
  EXPECT_EQ(payloads[2], "{\"new_utterances\": " + window + ", \"accepted_events\": [" + eventJson +
                             R"(, "target_id": "", "unresolved": false}]})");
}

TEST(PipelineFormatTest, DumpEscapesControlCharactersLikePython) {
  EXPECT_EQ(dumpJson(QJsonValue(QString("a\tb\x01\\ё"))), QByteArray("\"a\\tb\\u0001\\\\ё\""));
  EXPECT_EQ(dumpJson(json(R"({"b":1,"a":[true,null]})")), QByteArray(R"({"a": [true, null], "b": 1})"));
}

TEST(PipelineFormatTest, GemmaPromptTrimsTurnsAndOpensTheModelTurn) {
  EXPECT_EQ(renderGemmaPrompt("  SYS\n\n", " U \n"),
            QString("<|turn>system\nSYS<turn|>\n<|turn>user\nU<turn|>\n<|turn>model\n"));
}

TEST(PipelineFormatTest, ReplyParserAcceptsFencesAndRejectsProse) {
  EXPECT_EQ(parseModelReply("```json\n{\"events\":[]}\n```").toObject()["events"].toArray().size(), 0);
  EXPECT_TRUE(parseModelReply("  true ").toBool());
  EXPECT_THROW(parseModelReply("Here are the events"), PipelineError);
}
