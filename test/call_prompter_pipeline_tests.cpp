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
    return processRequest(request, [&](const QString &, const QByteArray &payload, const QByteArray &) {
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

TEST_F(PipelineTest, FailedMatchingAndHintKeepTheExtractedEvents) {
  request["candidates"] = json(R"([{"id":"a","text":"Документ","participants":["Анна"]}])");
  int calls = 0;
  const auto out = processRequest(request, [&](const QString &, const QByteArray &, const QByteArray &) -> QJsonValue {
    if (++calls == 1) return events(event());
    throw PipelineError("context budget exceeded");
  });
  ASSERT_EQ(out["events"].toArray().size(), 1);
  EXPECT_TRUE(out["events"][0]["unresolved"].toBool());
  EXPECT_TRUE(out["suggestions"].toArray().isEmpty());
}

TEST_F(PipelineTest, CancellationDuringMatchingEndsTheRequest) {
  request["candidates"] = json(R"([{"id":"a","text":"Документ","participants":["Анна"]}])");
  int calls = 0;
  EXPECT_THROW(processRequest(request, [&](const QString &, const QByteArray &, const QByteArray &) -> QJsonValue {
    if (++calls == 1) return events(event());
    throw PipelineError("cancelled");
  }), PipelineError);
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

TEST_F(PipelineTest, CancelOfARecordTheChangeAlreadyTargetsIsDropped) {
  request["candidates"] = json(R"([{"id":"meeting","text":"Встреча завтра в 15:00","participants":["Анна","Борис"]}])");
  const auto cancel = event({{"kind", "cancel"}, {"action", "Встреча завтра в 15:00"}, {"evidence_ids", QJsonArray{"1"}}});
  const auto moved = event({{"action", "Встреча в четверг в 16:00"}, {"evidence_ids", QJsonArray{"2"}}});
  // Both events resolve to the same record: the matcher finds the moved meeting too.
  const auto out = run({QJsonObject{{"events", QJsonArray{cancel, moved}}}, json(R"({"matches":["meeting"]})"), QJsonObject{}});
  ASSERT_EQ(out["events"].toArray().size(), 1);
  EXPECT_EQ(out["events"][0]["kind"].toString(), "accept");
  EXPECT_EQ(out["events"][0]["target_id"].toString(), "meeting");
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
    EXPECT_THROW(processRequest(broken, [](const QString &, const QByteArray &, const QByteArray &) { return QJsonValue(); }), PipelineError);
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
  EXPECT_TRUE(payloads[0].startsWith("{\"new_utterances\": " + window + ", \"call_date\": \""));
  EXPECT_EQ(payloads[1], "{\"event\": " + eventJson + R"(}, "records": [{"id": "r1", "participants": ["Анна"], "text": "Документ"}], "new_utterances": )" + window + "}");
  EXPECT_EQ(payloads[2], "{\"new_utterances\": " + window + ", \"accepted_events\": [" + eventJson +
                             R"(, "meeting": false, "date": "", "time": "", "target_id": "", "unresolved": false}]})");
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
  // Gemma sometimes wraps the object in prose or repeats it after a fence.
  EXPECT_EQ(parseModelReply("Вот заметки: {\"events\":[1]} Надеюсь, это поможет.").toObject()["events"].toArray().size(), 1);
  EXPECT_EQ(parseModelReply("```json\n{\"events\":[]}\n```\n```json\n{\"events\":[]}\n```").toObject().size(), 1);
  EXPECT_THROW(parseModelReply("{\"events\": [ unfinished"), PipelineError);
}

TEST(PipelineFailureReasonTest, GroupsFixedErrorCodesForTheUser) {
  using pcm::prompter::failureReason;
  EXPECT_EQ(failureReason("invalid notes evidence count"), "invalid_output");
  EXPECT_EQ(failureReason("invalid notes evidence source"), "invalid_output");
  EXPECT_EQ(failureReason("invalid notes output schema/budget"), "invalid_output");
  EXPECT_EQ(failureReason("reply is not JSON"), "invalid_output");
  EXPECT_EQ(failureReason("cloud auth"), "cloud");
  EXPECT_EQ(failureReason("cloud server"), "cloud");
  EXPECT_EQ(failureReason("cloud timeout"), "timeout");
  EXPECT_EQ(failureReason("output budget exceeded"), "invalid_output");
  EXPECT_EQ(failureReason("notes transcript budget exceeded"), "too_long");
  EXPECT_EQ(failureReason("context budget exceeded"), "too_long");
  EXPECT_EQ(failureReason("invalid notes source"), "runtime");
  EXPECT_EQ(failureReason("cancelled"), "cancelled");
  EXPECT_EQ(failureReason("unexpected"), "runtime");
}

namespace {
struct Call {
  QString system;
  QByteArray payload, schema;
};
QJsonObject parsed(const QByteArray &bytes) { return QJsonDocument::fromJson(bytes).object(); }
// Walks "properties" down a JSON schema, following array "items" and
// reflect-cpp's "$ref" into "$defs".
QJsonObject schemaAt(const QJsonObject &root, const QStringList &path) {
  const auto defs = root["$defs"].toObject();
  const auto deref = [&defs](QJsonObject node) {
    while (node.contains("$ref")) node = defs[node["$ref"].toString().section('/', -1)].toObject();
    return node;
  };
  auto schema = deref(root);
  for (const auto &key : path) {
    if (schema["type"] == "array") schema = deref(schema["items"].toObject());
    schema = deref(schema["properties"].toObject()[key].toObject());
  }
  return schema;
}
} // namespace

TEST(CalendarDays, StartsAtTheGivenDateWithRussianWeekdays) {
  const auto days = calendarDays(QDate(2026, 10, 29), 21);
  ASSERT_EQ(days.size(), 21);
  EXPECT_EQ(days.first().toObject(), (QJsonObject{{"date", "2026-10-29"}, {"weekday", "четверг"}}));
  EXPECT_EQ(days.last().toObject()["date"].toString(), "2026-11-18");
  EXPECT_EQ(days.last().toObject()["weekday"].toString(), "среда");
}

TEST_F(PipelineTest, ExtractCallPassesSchemaWithDateEnumFromCallDate) {
  request["call_date"] = "2026-10-29";
  std::vector<Call> calls;
  processRequest(request, [&](const QString &system, const QByteArray &payload, const QByteArray &schema) -> QJsonValue {
    calls.push_back({system, payload, schema});
    return calls.size() == 1 ? QJsonValue(events(event())) : QJsonValue(json(R"({"missing_deadline":false,"evidence_ids":[]})"));
  });
  ASSERT_FALSE(calls.empty());
  const auto &extract = calls.front();
  ASSERT_FALSE(extract.schema.isEmpty());
  EXPECT_TRUE(extract.payload.contains("\"calendar\""));
  EXPECT_TRUE(extract.payload.contains("2026-10-29 четверг"));
  EXPECT_TRUE(extract.payload.contains("2026-11-18 среда"));
  EXPECT_FALSE(extract.payload.contains("2026-11-19"));
  const auto dates = schemaAt(parsed(extract.schema), {"events", "date"})["enum"].toArray();
  EXPECT_TRUE(dates.contains("2026-11-05"));
  EXPECT_TRUE(dates.contains("2026-12-27")); // 60 days in the schema, 21 in the prompt
  EXPECT_TRUE(dates.contains(""));
  EXPECT_FALSE(dates.contains("2026-12-31"));
  const auto ids = schemaAt(parsed(extract.schema), {"events", "evidence_ids"})["items"].toObject()["enum"].toArray();
  EXPECT_EQ(ids, (QJsonArray{"1", "2"}));
}

TEST_F(PipelineTest, ExtractPayloadFitsTheAgreementInputBudget) {
  QJsonArray window;
  for (int i = 0; i < 24; ++i)
    window.append(QJsonObject{{"id", QString::number(1000 + i)}, {"speaker", i % 2 ? "Борис" : "Анна"},
                              {"text", QString(500, QChar(u'ж'))}});
  request["utterances"] = window;
  request["call_date"] = "2026-10-29";
  QByteArray payload;
  processRequest(request, [&](const QString &, const QByteArray &p, const QByteArray &) -> QJsonValue {
    if (payload.isEmpty()) payload = p;
    return json(R"({"events":[]})");
  });
  // 24 x 500 characters is the controller's largest window (12000-character cap);
  // the calendar adds about 0.5 KB of UTF-8 on top of it.
  EXPECT_LT(payload.size() - 24 * 1000, 2600);
}

TEST_F(PipelineTest, MatchAndDeadlineCallsAreSchemaConstrained) {
  request["candidates"] = json(R"([{"id":"a","text":"Документ","participants":["Анна","Борис"]},
                                   {"id":"b","text":"Встреча","participants":["Анна","Борис"]}])");
  std::vector<Call> calls;
  processRequest(request, [&](const QString &system, const QByteArray &payload, const QByteArray &schema) -> QJsonValue {
    calls.push_back({system, payload, schema});
    if (calls.size() == 1) return events(event());
    if (calls.size() == 2) return json(R"({"matches":["a"]})");
    return json(R"({"missing_deadline":false,"evidence_ids":[]})");
  });
  ASSERT_EQ(calls.size(), 3u);
  for (const auto &call : calls) EXPECT_FALSE(call.schema.isEmpty());
  EXPECT_EQ(schemaAt(parsed(calls[1].schema), {"matches"})["items"].toObject()["enum"].toArray(), (QJsonArray{"a", "b"}));
  EXPECT_EQ(schemaAt(parsed(calls[2].schema), {"evidence_ids"})["items"].toObject()["enum"].toArray(), (QJsonArray{"1", "2"}));
}

TEST_F(PipelineTest, MeetingFieldsSurviveIntoEvents) {
  request["call_date"] = "2026-10-29";
  const auto out = run({events(event({{"meeting", true}, {"date", "2026-11-05"}, {"time", "15:00"}})),
                        json(R"({"missing_deadline":false,"evidence_ids":[]})")});
  const auto e = out["events"][0].toObject();
  EXPECT_TRUE(e["meeting"].toBool());
  EXPECT_EQ(e["date"].toString(), "2026-11-05");
  EXPECT_EQ(e["time"].toString(), "15:00");
  EXPECT_TRUE(dumpJson(e).startsWith(R"({"kind": "accept", "participants": ["Анна", "Борис"], "action": "Анна отправит документ в пятницу", "evidence_ids": ["1", "2"], "meeting": true, "date": "2026-11-05", "time": "15:00")"));
}

TEST_F(PipelineTest, InvalidMeetingFieldsDropOnlyThoseFields) {
  request["call_date"] = "2026-10-29";
  const auto out = run({json(R"({"events":[
      {"kind":"accept","participants":["Анна","Борис"],"action":"Встреча","evidence_ids":["1","2"],"meeting":true,"date":"2027-01-01","time":"25:00"},
      {"kind":"accept","participants":["Анна","Борис"],"action":"Документ","evidence_ids":["1","2"]}]})"),
                        json(R"({"missing_deadline":false,"evidence_ids":[]})")});
  ASSERT_EQ(out["events"].toArray().size(), 2);
  const auto bad = out["events"][0].toObject(), missing = out["events"][1].toObject();
  EXPECT_TRUE(bad["meeting"].toBool());
  EXPECT_EQ(bad["date"].toString(), "");
  EXPECT_EQ(bad["time"].toString(), "");
  EXPECT_FALSE(missing["meeting"].toBool());
  EXPECT_EQ(missing["date"].toString(), "");
  EXPECT_EQ(missing["time"].toString(), "");
}
