#include "call_notes_pipeline.h"
#include "notes_common.h"

#include <QFile>
#include <QJsonDocument>
#include <QList>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <optional>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <string>
#include <vector>

namespace pcm::prompter {
namespace {
using namespace notes;
// Legacy budget of this pipeline (384 output tokens), kept until it is removed.
constexpr qsizetype kLegacyInputBytes = 4096 - 384 - 128;
constexpr qsizetype kSummaryBytes = 1100;

// The reply shape checkedSummary() accepts, for constrained decoding. Rules a
// schema cannot carry (an empty note has no evidence, the 1100-byte total)
// stay in checkedSummary().
using EvidenceIds = rfl::Validator<std::vector<std::string>, rfl::Size<rfl::Maximum<kMaxEvidenceIds>>>;
struct NoteSchema {
  rfl::Validator<std::string, rfl::Size<rfl::Maximum<160>>> text;
  EvidenceIds evidence_ids;
};
struct PointSchema {
  rfl::Validator<std::string, rfl::Size<rfl::Maximum<80>>> text;
  EvidenceIds evidence_ids;
};
struct SummarySchema {
  NoteSchema note;
  rfl::Validator<std::vector<PointSchema>, rfl::Size<rfl::Maximum<4>>> key_points;
};

// reflect-cpp's schema with every object closed and evidence_ids limited to
// the sources of this input. Edited as text: llguidance emits properties in
// schema order, so the note must stay before its points and each text before
// its evidence, as reflect-cpp writes them; QJsonObject would sort the keys.
QByteArray summarySchema(const QSet<QString> &allowed) {
  static const auto base = [] {
    auto schema = QByteArray::fromStdString(rfl::json::to_schema<SummarySchema>());
    schema.replace(R"("type":"object",)", R"("type":"object","additionalProperties":false,)");
    return schema;
  }();
  auto ids = QStringList(allowed.begin(), allowed.end());
  ids.sort();
  const auto names = QJsonDocument(QJsonArray::fromStringList(ids)).toJson(QJsonDocument::Compact);
  auto schema = base;
  return schema.replace(R"("evidence_ids":{"type":"array","items":{"type":"string"})",
                        R"("evidence_ids":{"type":"array","items":{"type":"string","enum":)" + names + "}");
}

QJsonObject emptySummary() {
  return {{"note", QJsonObject{{"text", ""}, {"evidence_ids", QJsonArray{}}}}, {"key_points", QJsonArray{}}};
}

QJsonObject checkedSummary(const QJsonValue &value, const QSet<QString> &allowed) {
  if (!value.isObject()) throw PipelineError("invalid notes output");
  const auto object = value.toObject();
  if (object.size() != 2 || !object["key_points"].isArray() || object["key_points"].toArray().size() > 4 ||
      serialize(object).size() > kSummaryBytes)
    throw PipelineError("invalid notes output schema/budget");
  const auto note = checkedItem(object["note"], 160, true, allowed);
  QJsonArray points;
  for (const auto &point : object["key_points"].toArray()) points.append(checkedItem(point, 80, false, allowed));
  return {{"note", note}, {"key_points", points}};
}

// Stands in for a merge reply the model got wrong: the first nonempty note and
// the points of both summaries, alternating, within the summary budget. Items
// are copied unchanged, so every one keeps the evidence it was validated with.
QJsonObject concatenated(const QJsonObject &a, const QJsonObject &b) {
  const auto note = a["note"].toObject()["text"].toString().isEmpty() ? b["note"] : a["note"];
  const auto left = a["key_points"].toArray(), right = b["key_points"].toArray();
  QJsonArray points;
  for (qsizetype i = 0; points.size() < 4 && (i < left.size() || i < right.size()); ++i) {
    if (i < left.size()) points.append(left[i]);
    if (i < right.size() && points.size() < 4) points.append(right[i]);
  }
  QJsonObject out{{"note", note}, {"key_points", points}};
  while (!points.isEmpty() && serialize(out).size() > kSummaryBytes) {
    points.removeLast();
    out["key_points"] = points;
  }
  return out;
}

void collectEvidence(const QJsonObject &summary, QSet<QString> &ids) {
  const auto add = [&ids](const QJsonObject &item) {
    for (const auto &id : item["evidence_ids"].toArray()) ids.insert(id.toString());
  };
  add(summary["note"].toObject());
  for (const auto &point : summary["key_points"].toArray()) add(point.toObject());
}

} // namespace

QJsonObject processNotes(const QJsonArray &utterances, const Generate &generate) {
  if (utterances.isEmpty()) return emptySummary();
  const auto notesPrompt = prompt("notes.txt");
  const auto mergePrompt = prompt("notes_merge.txt");
  // Prompt edits consume the same context as source JSON. Split using the
  // remaining capacity, rather than filling chunks to a fixed payload ceiling
  // and then rejecting them at the generator boundary.
  const auto leafBudget = std::min(kPayloadBytes, kLegacyInputBytes - notesPrompt.toUtf8().size());
  if (leafBudget <= 0) throw PipelineError("notes generation budget exceeded");
  const auto inputs = chunks(utterances, leafBudget);
  if (!generate) throw PipelineError("notes generator missing");
  int calls = 0;
  // Empty when the model's reply is unusable. Budget, cancellation and runtime
  // errors still end the request.
  const auto call = [&](const QString &system, const QJsonObject &input,
                        const QSet<QString> &allowed) -> std::optional<QJsonObject> {
    const auto payload = serialize(input);
    // Byte fallback provides a conservative token ceiling. Reserve 384 output
    // tokens and 128 for chat framing within the 4096-token model context.
    if (payload.size() > kPayloadBytes || system.toUtf8().size() + payload.size() > kLegacyInputBytes ||
        ++calls > 1023)
      throw PipelineError("notes generation budget exceeded");
    try {
      return checkedSummary(generate(system, payload, summarySchema(allowed)), allowed);
    } catch (const PipelineError &error) {
      if (failureReason(QString::fromLatin1(error.what())) != QLatin1String("invalid_output")) throw;
      return std::nullopt;
    }
  };
  const auto leaf = [&](const QJsonArray &input) {
    QSet<QString> allowed;
    for (const auto &u : input) allowed.insert(u.toObject()["id"].toString());
    return call(notesPrompt, {{"utterances", input}}, allowed);
  };
  // Decoding is greedy, so one unusable reply would recur on every update of a
  // growing call and block its whole summary. A rejected chunk is retried once
  // as two halves (different input, different reply); a half that still fails
  // is left out of the summary. Rejected replies are never used.
  QList<QJsonObject> level;
  for (const auto &input : inputs) {
    if (auto summary = leaf(input)) { level.append(*summary); continue; }
    if (input.size() < 2) continue;
    const auto half = input.size() / 2;
    QJsonArray first, second;
    for (qsizetype i = 0; i < input.size(); ++i) (i < half ? first : second).append(input[i]);
    for (const auto &part : {first, second})
      if (auto summary = leaf(part)) level.append(*summary);
  }
  if (level.isEmpty()) throw PipelineError("invalid notes output");
  while (level.size() > 1) {
    QList<QJsonObject> next;
    for (qsizetype i = 0; i < level.size(); i += 2) {
      if (i + 1 == level.size()) {
        next.append(level[i]);
        continue;
      }
      QSet<QString> allowed;
      collectEvidence(level[i], allowed);
      collectEvidence(level[i + 1], allowed);
      const auto merged = call(mergePrompt, {{"summaries", QJsonArray{level[i], level[i + 1]}}}, allowed);
      next.append(merged ? *merged : concatenated(level[i], level[i + 1]));
    }
    level = std::move(next);
  }
  return level.first();
}

} // namespace pcm::prompter
