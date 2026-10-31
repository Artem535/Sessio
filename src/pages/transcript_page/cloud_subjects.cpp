#include "cloud_subjects.h"

#include "app_settings.h"
#include "database.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QString>

namespace pcm::transcriptionui {
namespace {

void addWords(QJsonArray &out, QSet<QString> &seen, const QString &text) {
  for (const auto &word : text.split(QRegularExpression(QStringLiteral(R"([\s,;]+)")), Qt::SkipEmptyParts)) {
    const auto key = word.toLower();
    if (word.size() < 2 || seen.contains(key)) continue;
    seen.insert(key);
    out.append(word);
  }
}

} // namespace

QJsonObject cloudSubjects(pcm::database::Database &db, const QList<qint64> &transcriptIds, qint64 eventId) {
  QJsonArray client, specialist, phones, emails;
  QSet<QString> seenClient, seenSpecialist;
  QSet<int64_t> clients;
  if (eventId > 0) {
    try {
      for (const auto id : db.get_event_client_ids(eventId)) clients.insert(id);
    } catch (...) {}
  }
  for (const auto transcriptId : transcriptIds) {
    try {
      for (const auto id : db.get_transcript_client_ids(transcriptId)) clients.insert(id);
    } catch (...) {}
  }
  for (const auto id : clients) {
    try {
      const auto card = db.get_client(id);
      if (!card) continue;
      addWords(client, seenClient, QString::fromStdString(card->name.value_or("")));
      addWords(client, seenClient, QString::fromStdString(card->last_name.value_or("")));
      if (card->phone_number && !card->phone_number->empty()) phones.append(QString::fromStdString(*card->phone_number));
      if (card->email && !card->email->empty()) emails.append(QString::fromStdString(*card->email));
    } catch (...) {}
  }
  addWords(specialist, seenSpecialist, pcm::app_settings::callDisplayName());
  return {{"client", client}, {"specialist", specialist}, {"phones", phones}, {"emails", emails}};
}

} // namespace pcm::transcriptionui
