#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

namespace pcm::prompter {

// Who must not leave the machine by name. Names are words (first name, last
// name, patronymic); a person's words are matched in their Russian case forms.
struct PseudonymSubjects {
  QStringList clientNames;
  QStringList specialistNames;
  QStringList phones;
  QStringList emails;
};

// Replaces names, phone numbers and e-mail addresses by labels before a cloud
// request and puts them back in the reply. One instance serves one request:
// the table of replacements lives only in memory.
//   [Клиент] [Специалист] [Телефон N] [Email N]
// Phones and e-mails are also found by pattern in the text. Other details
// (third parties, places) are not touched. A label that stood for several
// spellings (Ивану, Ивана) comes back as the first one seen.
class Pseudonymizer {
public:
  explicit Pseudonymizer(const PseudonymSubjects &subjects);
  QString apply(const QString &text);
  // A serialised JSON payload: its text is rewritten in place, so key order and
  // formatting are kept.
  QByteArray applyJson(const QByteArray &jsonPayload);
  // Every string in the reply, recursively; unknown labels stay as they are.
  QJsonValue restore(const QJsonValue &reply) const;
  QString restoreText(const QString &text) const;

private:
  struct Person {
    QString label;
    QStringList stems;
  };
  QString labelFor(const QString &kind, const QString &key, const QString &surface);
  QString replaceNames(const QString &text);
  Person mClient, mSpecialist;
  QHash<QString, QString> mSurface;  // normalised label -> first original text
  QHash<QString, QString> mLabelByKey; // kind + key -> label
  QHash<QString, int> mCount;          // kind -> labels handed out
};

} // namespace pcm::prompter
