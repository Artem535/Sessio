#include "transcript_export.h"
#include "database.h"
#include <QCoreApplication>
#include <QFileDialog>
#include <QPointer>
#include <QSaveFile>
#include <QStringList>

namespace pcm::transcriptionui {
namespace {
// Shares the strings already translated for the transcript page.
QString tr(const char *text) { return QCoreApplication::translate("TranscriptPage", text); }

QString timestamp(std::int64_t milliseconds) {
  const auto seconds = milliseconds / 1000;
  if (seconds >= 3600)
    return QStringLiteral("%1:%2:%3").arg(seconds / 3600)
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
  return QStringLiteral("%1:%2").arg(seconds / 60, 2, 10, QLatin1Char('0'))
      .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
} // namespace

QString transcriptExportText(pcm::database::Database &db, std::int64_t transcriptId) {
  QStringList lines;
  for (const auto &phrase : db.get_transcript_phrases(transcriptId)) {
    const auto speaker = phrase.speaker_name ? QString::fromStdString(*phrase.speaker_name) :
        phrase.track_role == "practitioner" ? tr(QT_TRANSLATE_NOOP("TranscriptPage", "Practitioner")) : tr(QT_TRANSLATE_NOOP("TranscriptPage", "Participant"));
    lines << timestamp(phrase.start_ms) + " " + speaker + ": " + QString::fromStdString(phrase.text);
  }
  return lines.join("\n") + "\n";
}

bool exportTranscriptWithDialog(QWidget *parent, pcm::database::Database &db,
                                const std::vector<std::int64_t> &transcriptIds) {
  QPointer<QWidget> guard(parent);
  try {
    QStringList parts;
    for (const auto id : transcriptIds) parts << transcriptExportText(db, id);
    const auto bytes = parts.join("\n").toUtf8();
    const auto path = QFileDialog::getSaveFileName(parent, tr(QT_TRANSLATE_NOOP("TranscriptPage", "Export transcript")), {}, tr(QT_TRANSLATE_NOOP("TranscriptPage", "Text files (*.txt)")));
    if ((parent && !guard) || path.isEmpty()) return true;
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
  } catch (...) {
    return false;
  }
}
} // namespace pcm::transcriptionui
