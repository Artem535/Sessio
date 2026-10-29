#include "notes_common.h"

#include <QFile>
#include <QJsonDocument>
#include <cmath>

namespace pcm::prompter::notes {
namespace {
constexpr qsizetype kMaxChunks = 512;
bool textField(const QJsonValue &value, qsizetype max) {
  return value.isString() && !value.toString().trimmed().isEmpty() && value.toString().size() <= max;
}

bool timestamp(const QJsonValue &value) {
  if (!value.isDouble()) return false;
  const double n = value.toDouble();
  return std::isfinite(n) && n >= 0 && n <= 9007199254740991.0 && std::floor(n) == n;
}

} // namespace

QByteArray serialize(const QJsonObject &object) {
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QString prompt(const char *name) {
  QFile file(QStringLiteral(":/call-prompter/%1").arg(QString::fromLatin1(name)));
  if (!file.open(QIODevice::ReadOnly)) throw PipelineError("notes prompt resource missing");
  return QString::fromUtf8(file.readAll());
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
QSet<QString> ids(const QJsonArray &chunk) {
  QSet<QString> out;
  for (const auto &u : chunk) out.insert(u.toObject()["id"].toString());
  return out;
}

} // namespace pcm::prompter::notes
