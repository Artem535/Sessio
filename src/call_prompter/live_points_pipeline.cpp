#include "live_points_pipeline.h"
#include "notes_common.h"

#include <rfl.hpp>
#include <rfl/json.hpp>
#include <string>
#include <vector>

namespace pcm::prompter {
namespace {
using namespace notes;
constexpr qsizetype kMaxPointsPerRun = 3;
constexpr qsizetype kPointLength = 80;
constexpr qsizetype kKnownPoints = 10;

struct PointSchema {
  rfl::Validator<std::string, rfl::Size<rfl::Maximum<kPointLength>>> text;
  rfl::Validator<std::vector<std::string>, rfl::Size<rfl::Maximum<kMaxEvidenceIds>>> evidence_ids;
};
struct LivePointsSchema {
  rfl::Validator<std::vector<PointSchema>, rfl::Size<rfl::Maximum<kMaxPointsPerRun>>> key_points;
};

QByteArray schemaFor(const QSet<QString> &allowed) {
  static const auto base = closeObjects(QByteArray::fromStdString(rfl::json::to_schema<LivePointsSchema>()));
  auto sorted = QStringList(allowed.begin(), allowed.end());
  sorted.sort();
  return restrictStrings(base, "evidence_ids", sorted);
}

QJsonArray checkedPoints(const QJsonValue &value, const QSet<QString> &allowed) {
  const auto object = value.toObject();
  if (!value.isObject() || object.size() != 1 || !object["key_points"].isArray() ||
      object["key_points"].toArray().size() > kMaxPointsPerRun)
    throw PipelineError("invalid notes output schema/budget");
  QJsonArray out;
  for (const auto &item : object["key_points"].toArray()) out.append(checkedItem(item, kPointLength, false, allowed));
  return out;
}
} // namespace

QJsonObject processLivePoints(const QJsonArray &newUtterances, const QStringList &knownPoints,
                              const Generate &generate) {
  if (newUtterances.isEmpty()) return {{"key_points", QJsonArray{}}};
  const auto system = prompt("live_points.txt");
  auto known = QJsonArray::fromStringList(knownPoints.mid(std::max<qsizetype>(0, knownPoints.size() - kKnownPoints)));
  QSet<QString> seen;
  for (const auto &text : knownPoints) seen.insert(pointKey(text));
  const auto limit = std::min(kPayloadBytes, kContextInputBytes - system.toUtf8().size());
  const auto budget = limit - serialize({{"known_points", QJsonArray{}}}).size();
  if (budget <= 0) throw PipelineError("notes generation budget exceeded");
  const auto inputs = chunks(newUtterances, budget);
  if (!generate) throw PipelineError("notes generator missing");
  QJsonArray found;
  bool anyUsable = false;
  for (const auto &input : inputs) {
    const auto allowed = ids(input);
    // known_points first, as the prompt describes the payload.
    auto reference = known;
    const auto pack = [&] {
      return "{\"known_points\":" + QJsonDocument(reference).toJson(QJsonDocument::Compact) +
             ",\"utterances\":" + QJsonDocument(input).toJson(QJsonDocument::Compact) + "}";
    };
    while (!reference.isEmpty() && pack().size() > limit) reference.removeAt(0);
    const QByteArray payload = pack();
    try {
      for (const auto &point : checkedPoints(generate(system, payload, schemaFor(allowed)), allowed)) {
        const auto text = point.toObject()["text"].toString();
        if (seen.contains(pointKey(text))) continue;
        seen.insert(pointKey(text));
        found.append(point);
        known.append(text);
        if (known.size() > kKnownPoints) known.removeAt(0);
      }
      anyUsable = true;
    } catch (const PipelineError &error) {
      if (failureReason(QString::fromLatin1(error.what())) != QLatin1String("invalid_output")) throw;
    }
  }
  if (!anyUsable) throw PipelineError("invalid notes output");
  return {{"key_points", found}};
}

} // namespace pcm::prompter
