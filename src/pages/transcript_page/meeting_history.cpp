#include "meeting_history.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace {
QJsonObject parse(const std::string &json) {
  const auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));
  return doc.isObject() ? doc.object() : QJsonObject{};
}
QJsonObject shownPacket(const QJsonObject &state) {
  return state[state["final"].isObject() ? "final" : "live"].toObject();
}
}

std::vector<MeetingRecord> loadMeetingHistory(pcm::database::Database &db, int64_t clientId,
                                              qint64 excludeMaterialId) {
  std::vector<MeetingRecord> result;
  for (const auto &material : db.get_call_materials_for_client(clientId)) {
    if (material.id == excludeMaterialId) continue;
    const auto state = parse(material.json);
    if (!state["ended"].toBool()) continue;
    MeetingRecord record;
    record.materialId = material.id;
    record.startMs = material.created_at;
    if (material.event_id) {
      if (const auto event = db.get_event(*material.event_id)) {
        if (event->start_date) record.startMs = *event->start_date;
        if (event->start_date && event->end_date) record.durationMs = *event->end_date - *event->start_date;
      }
    }
    const auto packet = shownPacket(state);
    if (!packet["dismissed"].toBool()) {
      record.summary = packet["note"].toObject()["text"].toString().trimmed();
      record.keyPoints = packet["key_points"].toArray();
    }
    const auto entries = state["manual_entries"].toArray();
    record.noteCount = static_cast<int>(entries.size());
    if (!entries.isEmpty()) record.firstNote = entries.first().toObject()["body_markdown"].toString().trimmed();
    for (const auto transcript : db.get_call_material_transcript_ids(material.id)) {
      const auto prompter = parse(db.get_prompter_state(transcript).json);
      for (const auto &value : prompter["records"].toArray()) {
        const auto r = value.toObject();
        if (r["active"].toBool(true) && !r["text"].toString().trimmed().isEmpty())
          record.agreements.append(r["text"].toString().trimmed());
      }
    }
    result.push_back(std::move(record));
  }
  std::stable_sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.startMs > b.startMs; });
  return result;
}

QStringList loadNotesWithoutMeeting(pcm::database::Database &db, int64_t clientId) {
  auto notes = db.get_client_notes(clientId);
  std::stable_sort(notes.begin(), notes.end(), [](const auto &a, const auto &b) {
    return a.created_at.value_or(0) > b.created_at.value_or(0);
  });
  QStringList result;
  for (const auto &note : notes)
    if (!note.linked_event_id && !note.linked_series_id && note.body_markdown)
      result.append(QString::fromStdString(*note.body_markdown).trimmed());
  return result;
}

QString meetingPreview(const MeetingRecord &record) {
  const auto text = record.summary.isEmpty() ? record.firstNote : record.summary;
  auto lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  while (lines.size() > 2) lines.removeLast();
  auto preview = lines.join(QLatin1Char('\n'));
  if (preview.size() > 160) preview = preview.left(159) + QStringLiteral("…");
  return preview;
}
