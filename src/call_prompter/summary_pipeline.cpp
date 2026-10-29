#include "summary_pipeline.h"
#include "notes_common.h"

#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>
#include <optional>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <string>
#include <vector>

namespace pcm::prompter {
namespace {
using namespace notes;
constexpr qsizetype kOverviewLength = 1000;
constexpr qsizetype kGroupNoteLength = 300;
constexpr qsizetype kLeafNoteLength = 240;
constexpr qsizetype kPointLength = 120;
constexpr qsizetype kTaskLength = 160;
constexpr qsizetype kQuestionLength = 160;
constexpr qsizetype kMaxPoints = 10, kMaxTasks = 8, kMaxQuestions = 5;
constexpr qsizetype kLeafPoints = 3, kLeafTasks = 2, kLeafQuestions = 2;
// Leaf payloads list two weeks of dates; the schema accepts two months.
constexpr int kPromptCalendarDays = 14;
constexpr int kSchemaCalendarDays = 60;
// Accepted agreements sent as reference; the C++ check below is the guarantee.
constexpr qsizetype kAgreementBytes = 300;
constexpr int kMaxCalls = 1023;

using EvidenceIds = rfl::Validator<std::vector<std::string>, rfl::Size<rfl::Maximum<kMaxEvidenceIds>>>;
template <qsizetype N> struct Item {
  rfl::Validator<std::string, rfl::Size<rfl::Maximum<N>>> text;
  EvidenceIds evidence_ids;
};
struct TaskItem {
  rfl::Validator<std::string, rfl::Size<rfl::Maximum<kTaskLength>>> text;
  EvidenceIds evidence_ids;
  bool meeting;
  std::string date;
  std::string time;
};
struct LeafSchema {
  Item<kLeafNoteLength> note;
  rfl::Validator<std::vector<Item<kPointLength>>, rfl::Size<rfl::Maximum<kLeafPoints>>> key_points;
  rfl::Validator<std::vector<TaskItem>, rfl::Size<rfl::Maximum<kLeafTasks>>> tasks;
  rfl::Validator<std::vector<Item<kQuestionLength>>, rfl::Size<rfl::Maximum<kLeafQuestions>>> open_questions;
};
template <qsizetype N> struct OverviewSchema {
  Item<N> overview;
};

QStringList sorted(const QSet<QString> &set) {
  auto list = QStringList(set.begin(), set.end());
  list.sort();
  return list;
}

QString normalized(const QString &text) {
  auto value = text.toCaseFolded().simplified();
  while (!value.isEmpty() && QStringLiteral(".!?").contains(value.back())) value.chop(1);
  return value;
}

bool validTime(const QString &time) {
  static const QRegularExpression pattern(QStringLiteral("^([01][0-9]|2[0-3]):[0-5][0-9]$"));
  return pattern.match(time).hasMatch();
}

QJsonObject emptySummary() {
  return {{"format", 2}, {"overview", QJsonObject{{"text", ""}, {"evidence_ids", QJsonArray{}}}},
          {"key_points", QJsonArray{}}, {"tasks", QJsonArray{}}, {"open_questions", QJsonArray{}}};
}

struct Leaf {
  QJsonObject note;
  QJsonArray points, tasks, questions;
};

// Validates a leaf reply against its chunk's sources.
Leaf checkedLeaf(const QJsonValue &value, const QSet<QString> &allowed, const QSet<QString> &dates) {
  const auto object = value.toObject();
  if (!value.isObject() || object.size() != 4 || !object["key_points"].isArray() || !object["tasks"].isArray() ||
      !object["open_questions"].isArray() || object["key_points"].toArray().size() > kLeafPoints ||
      object["tasks"].toArray().size() > kLeafTasks || object["open_questions"].toArray().size() > kLeafQuestions)
    throw PipelineError("invalid notes output schema/budget");
  Leaf leaf;
  leaf.note = checkedItem(object["note"], kLeafNoteLength, true, allowed);
  for (const auto &p : object["key_points"].toArray()) leaf.points.append(checkedItem(p, kPointLength, false, allowed));
  for (const auto &q : object["open_questions"].toArray())
    leaf.questions.append(checkedItem(q, kQuestionLength, false, allowed));
  for (const auto &t : object["tasks"].toArray()) {
    const auto task = t.toObject();
    if (!t.isObject() || task.size() != 5 || !task["meeting"].isBool() || !task["date"].isString() ||
        !task["time"].isString())
      throw PipelineError("invalid notes item schema/length");
    auto out = checkedItem(QJsonObject{{"text", task["text"]}, {"evidence_ids", task["evidence_ids"]}}, kTaskLength,
                           false, allowed);
    // A wrong date or time only loses that detail.
    out["meeting"] = task["meeting"];
    out["date"] = dates.contains(task["date"].toString()) ? task["date"].toString() : QString();
    out["time"] = validTime(task["time"].toString()) ? task["time"].toString() : QString();
    leaf.tasks.append(out);
  }
  return leaf;
}

// Call order, repeats removed, then at most `limit` items spread evenly over
// the whole call so late facts are kept.
QJsonArray mergedList(const QList<QJsonArray> &lists, qsizetype limit, const QSet<QString> &excluded = {}) {
  QJsonArray all;
  QSet<QString> seen = excluded;
  for (const auto &list : lists)
    for (const auto &item : list) {
      const auto key = normalized(item.toObject()["text"].toString());
      if (seen.contains(key)) continue;
      seen.insert(key);
      all.append(item);
    }
  if (all.size() <= limit) return all;
  QJsonArray out;
  for (qsizetype i = 0; i < limit; ++i) out.append(all[i * (all.size() - 1) / (limit - 1)]);
  return out;
}

QJsonArray evidenceUnion(const QJsonArray &items) {
  QJsonArray ids;
  for (const auto &item : items)
    for (const auto &id : item.toObject()["evidence_ids"].toArray())
      if (!ids.contains(id) && ids.size() < kMaxEvidenceIds) ids.append(id);
  return ids;
}

// The notes' texts in order, cut at a word boundary within `limit`.
QJsonObject joinedNotes(const QJsonArray &notes, qsizetype limit) {
  QStringList parts;
  for (const auto &note : notes) parts << note.toObject()["text"].toString();
  auto text = parts.join(' ');
  if (text.size() > limit) {
    text.truncate(limit);
    const auto space = text.lastIndexOf(' ');
    if (space > limit / 2) text.truncate(space);
    if (text.back().isHighSurrogate()) text.chop(1);
  }
  return {{"text", text.trimmed()}, {"evidence_ids", evidenceUnion(notes)}};
}
} // namespace

QJsonObject processSummary(const QJsonArray &utterances, const SummaryContext &ctx, const Generate &generate) {
  if (utterances.isEmpty()) return emptySummary();
  const auto leafPrompt = prompt("summary_leaf.txt");
  const auto overviewPrompt = prompt("summary_overview.txt");

  const auto callDate = ctx.callDate.isValid() ? ctx.callDate : QDate::currentDate();
  QStringList promptDays;
  QSet<QString> dateSet;
  QStringList schemaDays{QString()};
  for (const auto &value : calendarDays(callDate, kSchemaCalendarDays)) {
    const auto day = value.toObject();
    schemaDays << day["date"].toString();
    dateSet.insert(day["date"].toString());
    if (promptDays.size() < kPromptCalendarDays) promptDays << day["date"].toString() + ' ' + day["weekday"].toString();
  }
  QJsonArray agreements;
  QSet<QString> accepted;
  qsizetype agreementBytes = 0;
  for (const auto &text : ctx.agreements) {
    accepted.insert(normalized(text));
    agreementBytes += text.toUtf8().size() + 3;
    if (agreementBytes <= kAgreementBytes) agreements.append(text);
  }
  // Everything but the utterances, in payload order.
  const QJsonObject reference{{"call_date", promptDays.first()}, {"calendar", QJsonArray::fromStringList(promptDays)},
                              {"agreements", agreements}};
  const auto referenceBytes = serialize(reference).size();
  const auto leafBudget = std::min(kPayloadBytes, kContextInputBytes - leafPrompt.toUtf8().size()) - referenceBytes;
  if (leafBudget <= 0) throw PipelineError("notes generation budget exceeded");
  const auto inputs = chunks(utterances, leafBudget);
  if (!generate) throw PipelineError("notes generator missing");

  static const auto leafBase = [] {
    auto schema = closeObjects(QByteArray::fromStdString(rfl::json::to_schema<LeafSchema>()));
    return schema.replace(R"("time":{"type":"string"})",
                          R"("time":{"type":"string","pattern":"^(([01][0-9]|2[0-3]):[0-5][0-9])?$"})");
  }();
  const auto leafDates = restrictStrings(leafBase, "date", schemaDays);

  int calls = 0;
  const auto call = [&](const QString &system, const QByteArray &payload, const QByteArray &schema) {
    if (system.toUtf8().size() + payload.size() > kContextInputBytes || ++calls > kMaxCalls)
      throw PipelineError("notes generation budget exceeded");
    return generate(system, payload, schema);
  };
  const auto usable = [](const PipelineError &error) {
    return failureReason(QString::fromLatin1(error.what())) == QLatin1String("invalid_output");
  };
  const auto leaf = [&](const QJsonArray &input) -> std::optional<Leaf> {
    const auto allowed = ids(input);
    // Reference fields first, as the prompt describes the payload.
    auto payload = serialize(reference);
    payload.chop(1);
    payload += ",\"utterances\":" + QJsonDocument(input).toJson(QJsonDocument::Compact) + '}';
    try {
      return checkedLeaf(call(leafPrompt, payload, restrictStrings(leafDates, "evidence_ids", sorted(allowed))), allowed,
                         dateSet);
    } catch (const PipelineError &error) {
      if (!usable(error)) throw;
      return std::nullopt;
    }
  };

  // Decoding is greedy: a rejected chunk would fail the same way again, so it
  // is retried once as two halves (different input, different reply).
  QList<Leaf> leaves;
  for (const auto &input : inputs) {
    if (auto result = leaf(input)) { leaves.append(*result); continue; }
    if (input.size() < 2) continue;
    const auto half = input.size() / 2;
    QJsonArray first, second;
    for (qsizetype i = 0; i < input.size(); ++i) (i < half ? first : second).append(input[i]);
    for (const auto &part : {first, second})
      if (auto result = leaf(part)) leaves.append(*result);
  }
  if (leaves.isEmpty()) throw PipelineError("invalid notes output");

  QList<QJsonArray> points, tasks, questions;
  QJsonArray notes;
  for (const auto &l : leaves) {
    points << l.points; tasks << l.tasks; questions << l.questions;
    if (!l.note["text"].toString().isEmpty()) notes.append(l.note);
  }

  // Overview: reduce the chunk notes in groups that fit one call until a
  // single group is left, then write the final overview from it.
  const auto overviewCall = [&](const QJsonArray &group, qsizetype limit) {
    QSet<QString> allowed;
    for (const auto &id : evidenceUnion(group)) allowed.insert(id.toString());
    for (const auto &note : group)
      for (const auto &id : note.toObject()["evidence_ids"].toArray()) allowed.insert(id.toString());
    const auto schema = restrictStrings(
        closeObjects(QByteArray::fromStdString(limit == kOverviewLength ? rfl::json::to_schema<OverviewSchema<kOverviewLength>>()
                                                                        : rfl::json::to_schema<OverviewSchema<kGroupNoteLength>>())),
        "evidence_ids", sorted(allowed));
    try {
      const auto reply = call(overviewPrompt, serialize({{"notes", group}}), schema).toObject();
      if (reply.size() != 1) throw PipelineError("invalid notes output schema/budget");
      return checkedItem(reply["overview"], limit, false, allowed);
    } catch (const PipelineError &error) {
      if (!usable(error)) throw;
      return joinedNotes(group, limit);
    }
  };
  const auto overviewBudget = kContextInputBytes - overviewPrompt.toUtf8().size();
  const auto fits = [overviewBudget](const QJsonArray &group) { return serialize({{"notes", group}}).size() <= overviewBudget; };
  QJsonObject overview{{"text", ""}, {"evidence_ids", QJsonArray{}}};
  while (!notes.isEmpty()) {
    if (fits(notes)) { overview = overviewCall(notes, kOverviewLength); break; }
    QJsonArray next, group;
    for (const auto &note : notes) {
      auto candidate = group;
      candidate.append(note);
      if (!group.isEmpty() && !fits(candidate)) {
        next.append(overviewCall(group, kGroupNoteLength));
        group = {};
      }
      group.append(note);
    }
    if (!group.isEmpty()) next.append(group.size() == 1 ? group.first().toObject() : overviewCall(group, kGroupNoteLength));
    // Every pass must shrink the list; a note too long to pair is not reducible.
    if (next.size() >= notes.size()) throw PipelineError("notes generation budget exceeded");
    notes = next;
  }

  auto out = emptySummary();
  out["overview"] = overview;
  out["key_points"] = mergedList(points, kMaxPoints);
  out["tasks"] = mergedList(tasks, kMaxTasks, accepted);
  out["open_questions"] = mergedList(questions, kMaxQuestions);
  return out;
}

} // namespace pcm::prompter
