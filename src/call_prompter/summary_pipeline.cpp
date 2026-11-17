#include "summary_pipeline.h"
#include "notes_common.h"

#include <QJsonDocument>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>
#include <optional>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <string>
#include <vector>

namespace pcm::prompter {
Q_DECLARE_LOGGING_CATEGORY(prompterDiagnostics)
Q_DECLARE_LOGGING_CATEGORY(prompterResponses)
namespace {
using namespace notes;
constexpr qsizetype kOverviewLength = 6000;
constexpr qsizetype kGroupNoteLength = 800;
constexpr qsizetype kLeafNoteLength = 2400;
constexpr qsizetype kPointLength = 120;
constexpr qsizetype kTaskLength = 160;
constexpr qsizetype kQuestionLength = 160;
constexpr qsizetype kMaxPoints = 20, kMaxTasks = 12, kMaxQuestions = 10;
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
struct EmptyNote {
  rfl::Validator<std::string, rfl::Size<rfl::Maximum<kLeafNoteLength>>> text;
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
  EmptyNote note;
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

// The words of an item, for telling a repeat from a different statement. A
// cut-off variant of the same statement shares its stems with the fuller one;
// a negation or a different number makes it another statement.
struct Words {
  QString key;
  QSet<QString> stems, numbers;
  bool negated = false;
};

Words wordsOf(const QString &text) {
  static const QSet<QString> negations{QStringLiteral("не"), QStringLiteral("нет"), QStringLiteral("без"),
                                       QStringLiteral("ни"), QStringLiteral("нельзя")};
  static const QRegularExpression separator(QStringLiteral(R"([^\p{L}\p{N}]+)"));
  Words out;
  out.key = normalized(text);
  for (const auto &word : out.key.split(separator, Qt::SkipEmptyParts)) {
    if (negations.contains(word)) out.negated = true;
    else if (word.front().isDigit()) out.numbers.insert(word);
    else if (word.size() > 2) out.stems.insert(word.left(5)); // crude stem: endings differ, roots do not
  }
  return out;
}

bool similar(const Words &a, const Words &b) {
  if (a.key == b.key) return true;
  if (a.negated != b.negated) return false;
  if (!a.numbers.isEmpty() && !b.numbers.isEmpty() && a.numbers != b.numbers) return false;
  const auto smaller = std::min(a.stems.size(), b.stems.size());
  if (smaller < 2) return false;
  const auto common = QSet<QString>(a.stems).intersect(b.stems).size();
  // A differing recipient or subject is meaningful, even in a long sentence.
  // Only a complete subset can be a shortened version of the same statement.
  return common == smaller;
}

// Source ids belong in evidence_ids; the model sometimes copies them into the prose too.
QString withoutSourceIds(QString text, const QSet<QString> &ids, const QSet<QString> &sourceNumbers) {
  QStringList numeric;
  for (const auto &id : ids)
    if (id.size() >= 3 && std::all_of(id.begin(), id.end(), [](QChar c) { return c.isDigit(); }))
      numeric << QRegularExpression::escape(id);
  if (numeric.isEmpty()) return text;
  const auto any = numeric.join(QLatin1Char('|'));
  constexpr auto options = QRegularExpression::UseUnicodePropertiesOption | QRegularExpression::CaseInsensitiveOption;
  const QRegularExpression wrapped(QStringLiteral(
      R"(\s*[(\[]\s*(?:(?:см\.?|реплик\w*|сообщени\w*|id)\s*[:№#]?\s*)?(?:%1)(?:[\s,;и]+(?:%1))*\s*[)\]])").arg(any), options);
  const QRegularExpression labelled(QStringLiteral(R"(\s*(?:реплик\w*|сообщени\w*|id)\s*[:№#]?\s*\b(?:%1)\b)").arg(any), options);
  const QRegularExpression beforePunctuation(QStringLiteral(R"(\s+([,.;:!?]))"));
  text.replace(wrapped, QString()).replace(labelled, QString());
  QStringList stray;
  for (const auto &id : ids)
    if (!sourceNumbers.contains(id) && numeric.contains(QRegularExpression::escape(id)))
      stray << QRegularExpression::escape(id);
  if (!stray.isEmpty()) {
    const QRegularExpression bare(QStringLiteral(R"(\s*[№#]?\s*\b(?:%1)\b)").arg(stray.join('|')), options);
    text.replace(bare, QString());
  }
  text.replace(beforePunctuation, QStringLiteral("\\1"));
  // Cutting an id at the end leaves the separator that preceded it.
  static const QRegularExpression dangling(QStringLiteral(R"([\s,;:\-–—]+$)"));
  return text.remove(dangling).simplified();
}

// Drops an item whose prose was nothing but source ids.
std::optional<QJsonObject> cleanedItem(QJsonObject item, const QSet<QString> &allowed,
                                      const QSet<QString> &sourceNumbers) {
  const auto text = withoutSourceIds(item["text"].toString(), allowed, sourceNumbers);
  if (text.isEmpty()) return std::nullopt;
  item["text"] = text;
  return item;
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
Leaf checkedLeaf(const QJsonValue &value, qsizetype noteLimit, const QSet<QString> &allowed,
                 const QSet<QString> &dates, const QSet<QString> &sourceNumbers) {
  const auto object = value.toObject();
  if (!value.isObject() || object.size() != 4 || !object["key_points"].isArray() || !object["tasks"].isArray() ||
      !object["open_questions"].isArray() || object["key_points"].toArray().size() > kLeafPoints ||
      object["tasks"].toArray().size() > kLeafTasks || object["open_questions"].toArray().size() > kLeafQuestions)
    throw PipelineError("invalid notes output schema/budget");
  Leaf leaf;
  leaf.note = checkedItem(object["note"], noteLimit, true, allowed);
  if (!leaf.note["text"].toString().isEmpty()) {
    if (const auto note = cleanedItem(leaf.note, allowed, sourceNumbers)) leaf.note = *note;
    else leaf.note = QJsonObject{{"text", ""}, {"evidence_ids", QJsonArray{}}};
  }
  for (const auto &p : object["key_points"].toArray())
    if (const auto point = cleanedItem(checkedItem(p, kPointLength, false, allowed), allowed, sourceNumbers)) leaf.points.append(*point);
  for (const auto &q : object["open_questions"].toArray())
    if (const auto question = cleanedItem(checkedItem(q, kQuestionLength, false, allowed), allowed, sourceNumbers))
      leaf.questions.append(*question);
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
    if (const auto cleaned = cleanedItem(out, allowed, sourceNumbers)) leaf.tasks.append(*cleaned);
  }
  return leaf;
}

QJsonArray evidenceUnion(const QJsonArray &items) {
  QJsonArray ids;
  for (const auto &item : items)
    for (const auto &id : item.toObject()["evidence_ids"].toArray())
      if (!ids.contains(id) && ids.size() < kMaxEvidenceIds) ids.append(id);
  return ids;
}

// Call order, repeats removed, then at most `limit` items spread evenly over
// the whole call so late facts are kept. A repeat is the same statement said
// again or a cut-off variant of it: the fuller wording is kept and cites both.
// Anything like an `excluded` text is dropped.
QJsonArray mergedList(const QList<QJsonArray> &lists, qsizetype limit, const QStringList &excluded = {}) {
  QList<Words> blocked;
  for (const auto &text : excluded) blocked.append(wordsOf(text));
  QJsonArray all;
  QList<Words> kept;
  for (const auto &list : lists)
    for (const auto &value : list) {
      const auto item = value.toObject();
      const auto words = wordsOf(item["text"].toString());
      if (std::any_of(blocked.begin(), blocked.end(), [&](const Words &b) { return similar(words, b); })) continue;
      qsizetype twin = -1;
      for (qsizetype i = 0; i < kept.size() && twin < 0; ++i)
        if (similar(words, kept[i])) {
          const auto previous = all[i].toObject();
          bool compatible = true;
          for (const char *field : {"meeting", "date", "time"})
            if (item.contains(field) && previous.contains(field) && item[field] != previous[field]) compatible = false;
          if (compatible) twin = i;
        }
      if (twin < 0) {
        all.append(item);
        kept.append(words);
        continue;
      }
      const auto first = all[twin].toObject();
      auto fuller = item["text"].toString().size() > first["text"].toString().size() ? item : first;
      fuller["evidence_ids"] = evidenceUnion(QJsonArray{first, item});
      all[twin] = fuller;
      kept[twin] = wordsOf(fuller["text"].toString());
    }
  if (all.size() <= limit) return all;
  QJsonArray out;
  for (qsizetype i = 0; i < limit; ++i) out.append(all[i * (all.size() - 1) / (limit - 1)]);
  return out;
}

// The notes' texts in order, cut at a word boundary within `limit`.
QJsonObject joinedNotes(const QJsonArray &notes, qsizetype limit) {
  // The parts often say the same thing; each sentence is kept once.
  static const QRegularExpression sentenceEnd(QStringLiteral(R"((?<=[.!?])\s+)"));
  QStringList parts;
  QList<Words> kept;
  for (const auto &note : notes)
    for (const auto &sentence : note.toObject()["text"].toString().split(sentenceEnd, Qt::SkipEmptyParts)) {
      const auto words = wordsOf(sentence);
      if (std::any_of(kept.begin(), kept.end(), [&](const Words &w) { return similar(words, w); })) continue;
      kept.append(words);
      parts << sentence;
    }
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
  static std::atomic_int nextRun = 0;
  const int run = ++nextRun;
  // A number spoken in the call may happen to equal a database source id.
  QSet<QString> sourceNumbers;
  static const QRegularExpression number(QStringLiteral(R"(\b[0-9]+\b)"));
  for (const auto &utterance : utterances) {
    auto matches = number.globalMatch(utterance.toObject()["text"].toString());
    while (matches.hasNext()) sourceNumbers.insert(matches.next().captured());
  }
  // User-configurable limits (QSettings via SummaryLimits) capped by the schema ceilings.
  const auto leafLimit = qBound(qsizetype(100), qsizetype(ctx.limits.leaf), kLeafNoteLength);
  const auto overviewLimit = qBound(qsizetype(500), qsizetype(ctx.limits.overview), kOverviewLength);
  static const QRegularExpression noteChars(QStringLiteral("<=\\d+ characters"));
  const auto leafPrompt =
      prompt("summary_leaf.txt").replace(noteChars, "<=" + QString::number(leafLimit) + " characters");
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
  qsizetype agreementBytes = 0;
  for (const auto &text : ctx.agreements) {
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
  qCInfo(prompterDiagnostics) << "Summary start run=" << run << "sources=" << utterances.size()
                             << "chunks=" << inputs.size() << "leaf_limit=" << leafLimit
                             << "overview_limit=" << overviewLimit;

  static const auto leafBase = [] {
    auto schema = closeObjects(QByteArray::fromStdString(rfl::json::to_schema<LeafSchema>()));
    return schema.replace(R"("time":{"type":"string"})",
                          R"("time":{"type":"string","pattern":"^(([01][0-9]|2[0-3]):[0-5][0-9])?$"})");
  }();
  const auto leafDates = restrictStrings(leafBase, "date", schemaDays);

  std::atomic_int calls = 0;
  const auto call = [&](const char *stage, const QString &system, const QByteArray &payload, const QByteArray &schema,
                        int *callId = nullptr) {
    const int index = ++calls;
    if (callId) *callId = index;
    if (system.toUtf8().size() + payload.size() > kContextInputBytes || index > kMaxCalls)
      throw PipelineError("notes generation budget exceeded");
    const auto input = QJsonDocument::fromJson(payload).object();
    const auto fullSchema = withReasoning(schema);
    qCInfo(prompterDiagnostics) << "Summary request run=" << run << "call=" << index << "stage=" << stage
                               << "sources=" << input["utterances"].toArray().size()
                               << "notes=" << input["notes"].toArray().size()
                               << "payload_bytes=" << payload.size() << "schema_bytes=" << fullSchema.size();
    // Opt-in only: these records contain meeting text, just like model replies.
    // The schema is omitted: it repeats per call and dwarfs the useful input.
    qCDebug(prompterResponses).noquote() << "Summary request"
        << QJsonDocument(QJsonObject{{"run", run}, {"call", index}, {"stage", QString::fromLatin1(stage)},
                                    {"system", system}, {"input", input}}).toJson(QJsonDocument::Compact);
    QElapsedTimer timer;
    timer.start();
    try {
      const auto reply = generate(system, payload, fullSchema);
      qCInfo(prompterDiagnostics) << "Summary reply run=" << run << "call=" << index << "stage=" << stage
                                 << "elapsed_ms=" << timer.elapsed() << "object=" << reply.isObject();
      qCDebug(prompterResponses).noquote() << "Summary parsed reply"
          << QJsonDocument(QJsonObject{{"run", run}, {"call", index}, {"stage", QString::fromLatin1(stage)},
                                      {"reply", reply}}).toJson(QJsonDocument::Compact);
      return reply;
    } catch (const PipelineError &error) {
      qCWarning(prompterDiagnostics) << "Summary generation failed run=" << run << "call=" << index
                                    << "stage=" << stage << "elapsed_ms=" << timer.elapsed()
                                    << "reason=" << failureReason(QString::fromLatin1(error.what()));
      throw;
    }
  };
  const auto usable = [](const PipelineError &error) {
    return failureReason(QString::fromLatin1(error.what())) == QLatin1String("invalid_output");
  };
  const auto leaf = [&](const QJsonArray &input) -> std::optional<Leaf> {
    const auto allowed = ids(input);
    // Reference fields first, as the prompt describes the payload.
    auto withHints = reference;
    QJsonArray hints;
    for (const auto &value : ctx.livePoints) {
      const auto hint = value.toObject();
      if (!value.isObject() || !hint["text"].isString() || hint["text"].toString().trimmed().isEmpty() ||
          !hint["evidence_ids"].isArray() || hint["evidence_ids"].toArray().isEmpty()) continue;
      bool local = true;
      for (const auto &id : hint["evidence_ids"].toArray())
        if (!id.isString() || !allowed.contains(id.toString())) { local = false; break; }
      if (!local) continue;
      auto candidate = hints;
      candidate.append(hint);
      auto trial = reference;
      trial["live_points"] = candidate;
      trial["utterances"] = input;
      if (leafPrompt.toUtf8().size() + serialize(trial).size() <= kContextInputBytes) hints = candidate;
    }
    if (!hints.isEmpty()) withHints["live_points"] = hints;
    Payload fields{{"call_date", withHints["call_date"]}, {"calendar", withHints["calendar"]},
                   {"agreements", withHints["agreements"]}};
    fields.emplace_back("utterances", input);
    if (withHints.contains("live_points")) fields.emplace_back("live_points", withHints["live_points"]);
    const auto payload = dumpPayload(fields);
    int attempt = 0;
    try {
      auto schema = restrictStrings(leafDates, "evidence_ids", sorted(allowed));
      const auto root = QJsonDocument::fromJson(schema).object();
      const auto defs = root["$defs"].toObject();
      const auto leafDef = defs[root["$ref"].toString().section('/', -1)].toObject();
      const auto noteDef = leafDef["properties"]["note"]["$ref"].toString().section('/', -1);
      auto result = checkedLeaf(call("leaf", leafPrompt, payload, requireNonemptyItems(schema, noteDef), &attempt), leafLimit,
                                allowed, dateSet, sourceNumbers);
      qCInfo(prompterDiagnostics) << "Summary leaf accepted run=" << run << "call=" << attempt << "sources=" << input.size()
                                 << "note_chars=" << result.note["text"].toString().size()
                                 << "points=" << result.points.size() << "tasks=" << result.tasks.size()
                                 << "questions=" << result.questions.size();
      return result;
    } catch (const PipelineError &error) {
      if (!usable(error)) throw;
      qCWarning(prompterDiagnostics) << "Leaf validation failed:" << error.what() << "run=" << run << "call=" << attempt
                                    << "sources=" << input.size();
      return std::nullopt;
    }
  };

  // Decoding is greedy: a rejected chunk would fail the same way again, so it
  // is retried once as two halves (different input, different reply).
  const auto leavesOf = [&](const QJsonArray &input) {
    QList<Leaf> out;
    if (auto result = leaf(input)) { out.append(*result); return out; }
    qCInfo(prompterDiagnostics) << "Summary leaf retry run=" << run << "sources=" << input.size()
                               << "halves=" << (input.size() >= 2);
    if (input.size() < 2) return out;
    const auto half = input.size() / 2;
    QJsonArray first, second;
    for (qsizetype i = 0; i < input.size(); ++i) (i < half ? first : second).append(input[i]);
    int partIndex = 0;
    for (const auto &part : {first, second}) {
      auto result = leaf(part);
      qCInfo(prompterDiagnostics) << "Summary retry half result run=" << run << "half=" << ++partIndex
                                 << "sources=" << part.size() << "accepted=" << result.has_value();
      if (result) out.append(*result);
    }
    return out;
  };
  // Chunks are independent; with parallelism above 1 they run on a few threads
  // and are collected in call order, so the result does not depend on timing.
  QList<QList<Leaf>> perInput(inputs.size());
  const auto threads = std::min<qsizetype>(std::max(ctx.parallelism, 1), inputs.size());
  if (threads <= 1) {
    for (qsizetype i = 0; i < inputs.size(); ++i) perInput[i] = leavesOf(inputs[i]);
  } else {
    std::atomic_int next = 0;
    std::atomic_bool stop = false;
    std::mutex failureMutex;
    std::exception_ptr failure;
    std::vector<std::thread> pool;
    for (qsizetype t = 0; t < threads; ++t)
      pool.emplace_back([&] {
        while (!stop) {
          const auto i = next++;
          if (i >= inputs.size()) return;
          try {
            perInput[i] = leavesOf(inputs[i]);
          } catch (...) {
            std::lock_guard lock(failureMutex);
            if (!failure) failure = std::current_exception();
            stop = true;
          }
        }
      });
    for (auto &thread : pool) thread.join();
    if (failure) std::rethrow_exception(failure);
  }
  QList<Leaf> leaves;
  for (qsizetype i = 0; i < perInput.size(); ++i) {
    qCInfo(prompterDiagnostics) << "Summary chunk result run=" << run << "chunk=" << i + 1
                               << "sources=" << inputs[i].size() << "accepted_leaves=" << perInput[i].size()
                               << "skipped=" << perInput[i].isEmpty();
    leaves.append(perInput[i]);
  }
  if (leaves.isEmpty()) throw PipelineError("invalid notes output");

  QList<QJsonArray> points, tasks, questions;
  QJsonArray notes;
  for (const auto &l : leaves) {
    points << l.points; tasks << l.tasks; questions << l.questions;
    if (!l.note["text"].toString().isEmpty()) notes.append(l.note);
  }
  qCInfo(prompterDiagnostics) << "Summary overview input run=" << run << "accepted_leaves=" << leaves.size()
                             << "notes=" << notes.size();
  for (qsizetype i = 0; i < notes.size(); ++i)
    qCInfo(prompterDiagnostics) << "Summary note run=" << run << "note=" << i + 1
                               << "chars=" << notes[i].toObject()["text"].toString().size()
                               << "evidence_count=" << notes[i].toObject()["evidence_ids"].toArray().size();

  // Overview: reduce the chunk notes in groups that fit one call until a
  // single group is left, then write the final overview from it.
  const auto overviewCall = [&](const QJsonArray &group, qsizetype limit, qsizetype schemaLimit) {
    QSet<QString> allowed;
    for (const auto &id : evidenceUnion(group)) allowed.insert(id.toString());
    for (const auto &note : group)
      for (const auto &id : note.toObject()["evidence_ids"].toArray()) allowed.insert(id.toString());
    const auto schema = restrictStrings(
        closeObjects(QByteArray::fromStdString(schemaLimit == kOverviewLength ? rfl::json::to_schema<OverviewSchema<kOverviewLength>>()
                                                                              : rfl::json::to_schema<OverviewSchema<kGroupNoteLength>>())),
        "evidence_ids", sorted(allowed));
    int attempt = 0;
    try {
      const auto reply = call("overview", overviewPrompt, serialize({{"notes", group}}), requireNonemptyItems(schema), &attempt).toObject();
      if (reply.size() != 1) throw PipelineError("invalid notes output schema/budget");
      const auto overview = checkedItem(reply["overview"], limit, false, allowed);
      if (const auto cleaned = cleanedItem(overview, ids(utterances), sourceNumbers)) {
        qCInfo(prompterDiagnostics) << "Summary overview accepted run=" << run << "call=" << attempt << "input_notes=" << group.size()
                                   << "chars=" << (*cleaned)["text"].toString().size();
        return *cleaned;
      }
      qCWarning(prompterDiagnostics) << "Summary overview empty after cleaning run=" << run << "call=" << attempt
                                    << "input_notes=" << group.size() << "fallback=joined_notes";
      return joinedNotes(group, limit);
    } catch (const PipelineError &error) {
      if (!usable(error)) throw;
      qCWarning(prompterDiagnostics) << "Overview validation failed:" << error.what() << "run=" << run << "call=" << attempt
                                    << "input_notes=" << group.size() << "fallback=joined_notes";
      return joinedNotes(group, limit);
    }
  };
  const auto overviewBudget = kContextInputBytes - overviewPrompt.toUtf8().size();
  const auto fits = [overviewBudget](const QJsonArray &group) { return serialize({{"notes", group}}).size() <= overviewBudget; };
  QJsonObject overview{{"text", ""}, {"evidence_ids", QJsonArray{}}};
  if (ctx.limits.partsOnly) {
    overview = joinedNotes(notes, overviewLimit);
    qCInfo(prompterDiagnostics) << "Summary overview joined run=" << run << "notes=" << notes.size()
                               << "chars=" << overview["text"].toString().size() << "mode=parts";
  } else while (!notes.isEmpty()) {
    if (fits(notes)) { overview = overviewCall(notes, overviewLimit, kOverviewLength); break; }
    QJsonArray next, group;
    for (const auto &note : notes) {
      auto candidate = group;
      candidate.append(note);
      if (!group.isEmpty() && !fits(candidate)) {
        next.append(overviewCall(group, kGroupNoteLength, kGroupNoteLength));
        group = {};
      }
      group.append(note);
    }
    if (!group.isEmpty()) next.append(group.size() == 1 ? group.first().toObject() : overviewCall(group, kGroupNoteLength, kGroupNoteLength));
    // Every pass must shrink the list; a note too long to pair is not reducible.
    if (next.size() >= notes.size()) throw PipelineError("notes generation budget exceeded");
    notes = next;
  }

  auto out = emptySummary();
  out["overview"] = overview;
  out["key_points"] = mergedList(points, qBound(qsizetype(2), qsizetype(ctx.limits.points), kMaxPoints));
  out["tasks"] = mergedList(tasks, qBound(qsizetype(2), qsizetype(ctx.limits.tasks), kMaxTasks), ctx.agreements);
  out["open_questions"] = mergedList(questions, qBound(qsizetype(2), qsizetype(ctx.limits.questions), kMaxQuestions));
  qCInfo(prompterDiagnostics) << "Summary complete run=" << run << "calls=" << calls.load()
                             << "overview_chars=" << overview["text"].toString().size()
                             << "points=" << out["key_points"].toArray().size()
                             << "tasks=" << out["tasks"].toArray().size()
                             << "questions=" << out["open_questions"].toArray().size();
  return out;
}

} // namespace pcm::prompter
