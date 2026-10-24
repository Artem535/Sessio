#include "call_notes_pipeline.h"

#include <QFile>
#include <QJsonDocument>
#include <QList>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace pcm::prompter {
namespace {
constexpr qsizetype kPayloadBytes = 2400;
// Byte-fallback token ceiling, excluding the existing output/framing reserves.
constexpr qsizetype kContextInputBytes = 4096 - 384 - 128;
constexpr qsizetype kSummaryBytes = 1100;
constexpr qsizetype kMaxChunks = 512;
// Real Gemma cites all five repeated-topic sources in the synthetic long-call
// smoke. Retain them rather than dropping provenance; serialized reply and
// merge-input budgets still cap the total cost of these original IDs.
constexpr qsizetype kMaxEvidenceIds = 8;

QByteArray serialize(const QJsonObject &object) {
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QString prompt(const char *name) {
  QFile file(QStringLiteral(":/call-prompter/%1").arg(QString::fromLatin1(name)));
  if (!file.open(QIODevice::ReadOnly)) throw PipelineError("notes prompt resource missing");
  return QString::fromUtf8(file.readAll());
}

bool textField(const QJsonValue &value, qsizetype max) {
  return value.isString() && !value.toString().trimmed().isEmpty() && value.toString().size() <= max;
}

bool timestamp(const QJsonValue &value) {
  if (!value.isDouble()) return false;
  const double n = value.toDouble();
  return std::isfinite(n) && n >= 0 && n <= 9007199254740991.0 && std::floor(n) == n;
}

QJsonObject emptySummary() {
  return {{"note", QJsonObject{{"text", ""}, {"evidence_ids", QJsonArray{}}}}, {"key_points", QJsonArray{}}};
}

QJsonObject checkedItem(const QJsonValue &value, qsizetype limit, bool allowEmpty,
                        const QSet<QString> &allowed) {
  if (!value.isObject()) throw PipelineError("invalid notes item");
  auto item = value.toObject();
  if (item.size() != 2 || !item["text"].isString() || !item["evidence_ids"].isArray() ||
      item["text"].toString().size() > limit)
    throw PipelineError("invalid notes item schema/length");
  const QString text = item["text"].toString().trimmed();
  const auto ids = item["evidence_ids"].toArray();
  if (text.isEmpty()) {
    if (!allowEmpty || !ids.isEmpty()) throw PipelineError("invalid empty notes item");
  } else if (ids.isEmpty() || ids.size() > kMaxEvidenceIds) {
    throw PipelineError("invalid notes evidence count");
  }
  QSet<QString> seen;
  for (const auto &id : ids) {
    if (!id.isString() || !allowed.contains(id.toString()) || seen.contains(id.toString()))
      throw PipelineError("invalid notes evidence source");
    seen.insert(id.toString());
  }
  item["text"] = text;
  return item;
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

void collectEvidence(const QJsonObject &summary, QSet<QString> &ids) {
  const auto add = [&ids](const QJsonObject &item) {
    for (const auto &id : item["evidence_ids"].toArray()) ids.insert(id.toString());
  };
  add(summary["note"].toObject());
  for (const auto &point : summary["key_points"].toArray()) add(point.toObject());
}

// Preflight the entire transcript before any inference. Pairwise reduction
// strictly halves the number of summaries at each level; no model-controlled
// retry or reduction loop can consume an unbounded number of calls.
QList<QJsonArray> chunks(const QJsonArray &utterances, qsizetype payloadBudget) {
  if (utterances.size() > 1024) throw PipelineError("notes source count exceeded");
  QSet<QString> ids;
  qsizetype total = 0;
  for (const auto &value : utterances) {
    if (!value.isObject()) throw PipelineError("invalid notes source");
    const auto u = value.toObject();
    if (!textField(u["id"], 80) || ids.contains(u["id"].toString()) || !textField(u["speaker"], 200) ||
        !textField(u["text"], 6000) || !timestamp(u["start_ms"]) || !timestamp(u["end_ms"]) ||
        u["end_ms"].toDouble() < u["start_ms"].toDouble())
      throw PipelineError("invalid notes source fields");
    ids.insert(u["id"].toString());
    total += u["text"].toString().size();
    if (total > 256000) throw PipelineError("notes transcript budget exceeded");
  }

  QList<QJsonArray> result;
  QJsonArray current;
  const auto flush = [&]() {
    if (current.isEmpty()) return;
    if (result.size() >= kMaxChunks) throw PipelineError("notes chunk budget exceeded");
    result.append(current);
    current = {};
  };
  const auto fits = [payloadBudget](const QJsonArray &array) {
    return serialize({{"utterances", array}}).size() <= payloadBudget;
  };
  for (const auto &value : utterances) {
    const auto u = value.toObject();
    // Only approved source fields reach the model; unknown metadata cannot
    // inject instructions or silently consume its context.
    QJsonObject fragment{{"id", u["id"]}, {"speaker", u["speaker"]},
                         {"start_ms", u["start_ms"]}, {"end_ms", u["end_ms"]}};
    const auto text = u["text"].toString();
    qsizetype offset = 0;
    while (offset < text.size()) {
      fragment["text"] = text.mid(offset);
      auto candidate = current;
      candidate.append(fragment);
      if (fits(candidate)) {
        current = candidate;
        break;
      }
      if (!current.isEmpty()) {
        flush();
        continue;
      }
      // Find a fitting prefix, then back off a split UTF-16 surrogate pair.
      qsizetype lo = 0, hi = text.size() - offset;
      while (lo < hi) {
        const qsizetype mid = lo + (hi - lo + 1) / 2;
        fragment["text"] = text.mid(offset, mid);
        if (fits(QJsonArray{fragment})) lo = mid;
        else hi = mid - 1;
      }
      if (lo > 0 && offset + lo < text.size() && text[offset + lo - 1].isHighSurrogate() &&
          text[offset + lo].isLowSurrogate()) --lo;
      if (lo == 0) throw PipelineError("notes source metadata budget exceeded");
      fragment["text"] = text.mid(offset, lo);
      current.append(fragment);
      offset += lo;
      flush();
    }
  }
  flush();
  return result;
}
} // namespace

QJsonObject processNotes(const QJsonArray &utterances, const Generate &generate) {
  if (utterances.isEmpty()) return emptySummary();
  const auto notesPrompt = prompt("notes.txt");
  const auto mergePrompt = prompt("notes_merge.txt");
  // Prompt edits consume the same context as source JSON. Split using the
  // remaining capacity, rather than filling chunks to a fixed payload ceiling
  // and then rejecting them at the generator boundary.
  const auto leafBudget = std::min(kPayloadBytes, kContextInputBytes - notesPrompt.toUtf8().size());
  if (leafBudget <= 0) throw PipelineError("notes generation budget exceeded");
  const auto inputs = chunks(utterances, leafBudget);
  if (!generate) throw PipelineError("notes generator missing");
  int calls = 0;
  const auto call = [&](const QString &system, const QJsonObject &input, const QSet<QString> &allowed) {
    const auto payload = serialize(input);
    // Byte fallback provides a conservative token ceiling. Reserve 384 output
    // tokens and 128 for chat framing within the 4096-token model context.
    if (payload.size() > kPayloadBytes || system.toUtf8().size() + payload.size() > kContextInputBytes ||
        ++calls > 1023)
      throw PipelineError("notes generation budget exceeded");
    // Cancellation belongs to the generator boundary, with no catch/retry.
    return checkedSummary(generate(system, payload), allowed);
  };
  QList<QJsonObject> level;
  for (const auto &input : inputs) {
    QSet<QString> allowed;
    for (const auto &u : input) allowed.insert(u.toObject()["id"].toString());
    level.append(call(notesPrompt, {{"utterances", input}}, allowed));
  }
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
      next.append(call(mergePrompt, {{"summaries", QJsonArray{level[i], level[i + 1]}}}, allowed));
    }
    level = std::move(next);
  }
  return level.first();
}

} // namespace pcm::prompter
