#pragma once

#include <QString>
#include <cstdint>
#include <vector>

class QWidget;
namespace pcm::database { class Database; }

namespace pcm::transcriptionui {
// Plain text of a transcript: one "mm:ss Speaker: text" line per phrase.
QString transcriptExportText(pcm::database::Database &db, std::int64_t transcriptId);
// Asks for a .txt path and writes the transcripts there, one after another.
// Returns false when the write failed; cancelling the dialog is not a failure.
bool exportTranscriptWithDialog(QWidget *parent, pcm::database::Database &db,
                                const std::vector<std::int64_t> &transcriptIds);
}
