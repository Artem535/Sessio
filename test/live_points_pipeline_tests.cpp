#include "live_points_pipeline.h"

#include <QJsonDocument>
#include <QStringList>
#include <gtest/gtest.h>

using namespace pcm::prompter;

namespace {
QJsonObject source(const QString &id, const QString &text = QStringLiteral("Обсудили проект.")) {
  return {{"id", id}, {"speaker", "A"}, {"text", text}, {"start_ms", 0}, {"end_ms", 1000}};
}
QJsonObject point(const QString &text, const QJsonArray &ids) { return {{"text", text}, {"evidence_ids", ids}}; }
QJsonObject points(const QJsonArray &items) { return {{"key_points", items}}; }
QJsonObject parsed(const QByteArray &bytes) { return QJsonDocument::fromJson(bytes).object(); }
// Utterances whose serialized size forces one chunk each.
QJsonArray longWindow(int count) {
  QJsonArray out;
  for (int i = 0; i < count; ++i) out.append(source("u" + QString::number(i), QString(700, QChar(u'ж'))));
  return out;
}
QJsonArray evidenceEnum(const QByteArray &schema) {
  const auto root = parsed(schema);
  const auto defs = root["$defs"].toObject();
  for (const auto &def : defs)
    if (def.toObject()["properties"].toObject().contains("evidence_ids"))
      return def.toObject()["properties"].toObject()["evidence_ids"].toObject()["items"].toObject()["enum"].toArray();
  return {};
}
} // namespace

TEST(LivePointsPipeline, EmptyInputDoesNotInvokeModel) {
  int calls = 0;
  const auto out = processLivePoints({}, {}, [&](const QString &, const QByteArray &, const QByteArray &) {
    ++calls;
    return QJsonValue(points({}));
  });
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(out, points({}));
}

TEST(LivePointsPipeline, PayloadHasOnlyNewUtterancesAndKnownPointTexts) {
  QByteArray payload;
  processLivePoints({source("u5")}, {QStringLiteral("Старый пункт")},
                    [&](const QString &, const QByteArray &p, const QByteArray &) {
                      payload = p;
                      return QJsonValue(points({}));
                    });
  const auto input = parsed(payload);
  EXPECT_EQ(input["known_points"].toArray(), (QJsonArray{"Старый пункт"}));
  ASSERT_EQ(input["utterances"].toArray().size(), 1);
  EXPECT_EQ(input["utterances"][0].toObject()["id"].toString(), "u5");
}

TEST(LivePointsPipeline, KnownPointsAreCappedToTheTenMostRecent) {
  QStringList known;
  for (int i = 0; i < 15; ++i) known << QStringLiteral("Пункт %1").arg(i);
  QByteArray payload;
  processLivePoints({source("u1")}, known, [&](const QString &, const QByteArray &p, const QByteArray &) {
    payload = p;
    return QJsonValue(points({}));
  });
  const auto sent = parsed(payload)["known_points"].toArray();
  ASSERT_EQ(sent.size(), 10);
  EXPECT_EQ(sent.first().toString(), "Пункт 5");
  EXPECT_EQ(sent.last().toString(), "Пункт 14");
}

TEST(LivePointsPipeline, SchemaEnumCoversOnlyChunkIds) {
  QByteArray schema;
  processLivePoints({source("u1"), source("u2")}, {}, [&](const QString &, const QByteArray &, const QByteArray &s) {
    schema = s;
    return QJsonValue(points({}));
  });
  ASSERT_FALSE(schema.isEmpty());
  EXPECT_EQ(evidenceEnum(schema), (QJsonArray{"u1", "u2"}));
  EXPECT_TRUE(schema.contains(R"("additionalProperties":false)"));
}

TEST(LivePointsPipeline, AcceptsValidPointsAndTrimsText) {
  const auto out = processLivePoints({source("u1")}, {}, [](const QString &, const QByteArray &, const QByteArray &) {
    return QJsonValue(points({point(QStringLiteral("  Проект согласован "), {"u1"})}));
  });
  EXPECT_EQ(out["key_points"].toArray(), (QJsonArray{point(QStringLiteral("Проект согласован"), {"u1"})}));
}

TEST(LivePointsPipeline, RepeatedKnownAndSameBatchPointsAreNotReturned) {
  const auto out = processLivePoints({source("u1"), source("u2")}, {"Звук записывается нормально."},
      [](const QString &, const QByteArray &, const QByteArray &) {
        return QJsonValue(points({point(" звук  записывается нормально! ", {"u1"}),
                                 point("Для записи выбран OBS", {"u1"}),
                                 point("Для записи выбран OBS.", {"u2"})}));
      });
  EXPECT_EQ(out["key_points"].toArray(), (QJsonArray{point("Для записи выбран OBS", {"u1"})}));
}

TEST(LivePointsPipeline, LaterChunksKnowPointsFoundEarlierInTheSameRun) {
  int calls = 0;
  const auto out = processLivePoints(longWindow(2), {}, [&](const QString &, const QByteArray &payload, const QByteArray &) {
    const auto input = parsed(payload);
    if (++calls == 2) EXPECT_TRUE(input["known_points"].toArray().contains("Решение принято"));
    const auto id = input["utterances"][0].toObject()["id"].toString();
    return QJsonValue(points({point("Решение принято", {id})}));
  });
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(out["key_points"].toArray().size(), 1);
}

TEST(LivePointsPipeline, CitingKnownOrForeignIdRejectsThatChunk) {
  for (const auto &ids : {QJsonArray{"old"}, QJsonArray{}, QJsonArray{"u1", "u1"}}) {
    EXPECT_THROW(processLivePoints({source("u1")}, {QStringLiteral("old")},
                                   [&](const QString &, const QByteArray &, const QByteArray &) {
                                     return QJsonValue(points({point(QStringLiteral("Факт"), ids)}));
                                   }),
                 PipelineError);
  }
}

TEST(LivePointsPipeline, AtMostThreePointsPerChunkAndEightyChars) {
  const auto run = [](const QJsonArray &items) {
    return processLivePoints({source("u1")}, {}, [&](const QString &, const QByteArray &, const QByteArray &) {
      return QJsonValue(points(items));
    });
  };
  QJsonArray four;
  for (int i = 0; i < 4; ++i) four.append(point(QStringLiteral("Факт %1").arg(i), {"u1"}));
  EXPECT_THROW(run(four), PipelineError);
  EXPECT_THROW(run({point(QString(81, QChar(u'я')), {"u1"})}), PipelineError);
  EXPECT_EQ(run({point(QString(80, QChar(u'я')), {"u1"})})["key_points"].toArray().size(), 1);
}

TEST(LivePointsPipeline, LongWindowIsChunkedAndPointsConcatenate) {
  int calls = 0;
  const auto out = processLivePoints(longWindow(3), {}, [&](const QString &, const QByteArray &payload, const QByteArray &) {
    ++calls;
    const auto first = parsed(payload)["utterances"][0].toObject()["id"].toString();
    return QJsonValue(points({point("Факт " + first, {first})}));
  });
  EXPECT_EQ(calls, 3);
  const auto items = out["key_points"].toArray();
  ASSERT_EQ(items.size(), 3);
  EXPECT_EQ(items[0].toObject()["text"].toString(), "Факт u0");
  EXPECT_EQ(items[2].toObject()["text"].toString(), "Факт u2");
}

TEST(LivePointsPipeline, OneFailedChunkIsSkippedAllFailedThrows) {
  int calls = 0;
  const auto out = processLivePoints(longWindow(3), {}, [&](const QString &, const QByteArray &payload, const QByteArray &) {
    const auto first = parsed(payload)["utterances"][0].toObject()["id"].toString();
    if (++calls == 2) return QJsonValue(QStringLiteral("not json"));
    return QJsonValue(points({point("Факт " + first, {first})}));
  });
  EXPECT_EQ(out["key_points"].toArray().size(), 2);
  EXPECT_THROW(processLivePoints(longWindow(2), {}, [](const QString &, const QByteArray &, const QByteArray &) {
                 return QJsonValue(QStringLiteral("not json"));
               }),
               PipelineError);
}

TEST(LivePointsPipeline, RuntimeErrorsPropagate) {
  EXPECT_THROW(processLivePoints({source("u1")}, {}, [](const QString &, const QByteArray &, const QByteArray &) -> QJsonValue {
                 throw PipelineError("cancelled");
               }),
               PipelineError);
}
