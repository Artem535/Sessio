#pragma once
#include "database.h"
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <cstdint>
#include <vector>

struct MeetingRecord {
  qint64 materialId = 0;
  qint64 startMs = 0;        // event start, else material created_at
  qint64 durationMs = 0;     // event duration, 0 when unknown
  QString summary;           // AI summary text (final, else live; dismissed -> empty)
  QString firstNote;         // first own note entry, Markdown source
  int noteCount = 0;         // own note entries
  QStringList agreements;    // active prompter records of linked transcripts
  QJsonArray keyPoints;      // key points of the shown summary
};

// Ended meetings of the client, newest first, without excludeMaterialId.
std::vector<MeetingRecord> loadMeetingHistory(pcm::database::Database &db, int64_t clientId,
                                              qint64 excludeMaterialId);
// Prompter records of every transcript linked to the material, in storage order.
QJsonArray loadMaterialAgreements(pcm::database::Database &db, int64_t materialId);
// Client notes without a linked event or series, newest first (Markdown).
QStringList loadNotesWithoutMeeting(pcm::database::Database &db, int64_t clientId);
// Two-line preview: summary, else first note; trimmed to 2 lines / 160 chars.
QString meetingPreview(const MeetingRecord &record);
