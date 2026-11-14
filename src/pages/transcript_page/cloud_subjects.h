#pragma once

#include <QJsonObject>
#include <QList>
#include <memory>

namespace pcm::database { class Database; }

namespace pcm::transcriptionui {

// The people and contact details a cloud request must not carry by name, from
// the cards of the clients linked to these transcripts, the client named on the
// call's event (a live call has no transcript client yet) and the specialist's
// own display name: {client:[words], specialist:[words], phones:[], emails:[]}.
// A card the database cannot read is skipped. Speaker labels are not added: a
// name that is neither on a card nor in the specialist's name is not hidden.
QJsonObject cloudSubjects(pcm::database::Database &db, const QList<qint64> &transcriptIds, qint64 eventId = 0);

} // namespace pcm::transcriptionui
