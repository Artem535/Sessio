#include "pseudonymizer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

namespace pcm::prompter {
namespace {

const QRegularExpression &emailPattern() {
  static const QRegularExpression re(QStringLiteral(R"([\w.+\-]+@[\w\-]+(?:\.[\w\-]+)+)"),
                                     QRegularExpression::UseUnicodePropertiesOption);
  return re;
}
const QRegularExpression &phonePattern() {
  static const QRegularExpression re(QStringLiteral(R"((?<![\d\w])\+?\d[\d\s\-().]{7,}\d(?![\d\w]))"));
  return re;
}
// Escapes and existing labels pass through; words are what names are matched on.
const QRegularExpression &wordPattern() {
  static const QRegularExpression re(QStringLiteral(R"(\\.|\[[^\[\]]{1,24}\]|\p{L}[\p{L}\-']*)"),
                                     QRegularExpression::UseUnicodePropertiesOption);
  return re;
}
const QRegularExpression &labelPattern() {
  static const QRegularExpression re(QStringLiteral(R"(\[\s*([^\[\]]{1,24}?)\s*\])"));
  return re;
}

QString normalisedLabel(QString text) {
  return text.simplified().toLower();
}

// The part of a name that survives Russian declension.
QString stemOf(const QString &name) {
  auto stem = name.toLower();
  if (stem.size() >= 4 && QStringLiteral("аяйь").contains(stem.back())) stem.chop(1);
  return stem;
}

bool matchesStem(const QString &word, const QString &stem) {
  if (stem.size() < 2 || !word.startsWith(stem)) return false;
  const auto extra = word.size() - stem.size();
  return extra <= (stem.size() <= 3 ? 2 : 3);
}

QString digitsOf(const QString &text) {
  QString digits;
  for (const auto ch : text) if (ch.isDigit()) digits += ch;
  return digits;
}

} // namespace

Pseudonymizer::Pseudonymizer(const PseudonymSubjects &subjects) {
  mClient.label = QStringLiteral("[Клиент]");
  mSpecialist.label = QStringLiteral("[Специалист]");
  for (const auto &name : subjects.clientNames) mClient.stems << stemOf(name.trimmed());
  for (const auto &name : subjects.specialistNames) mSpecialist.stems << stemOf(name.trimmed());
  // Subject phones and e-mails take the first numbers.
  for (const auto &phone : subjects.phones) {
    auto digits = digitsOf(phone);
    if (digits.size() >= 10) labelFor(QStringLiteral("Телефон"), digits.right(10), QString());
  }
  for (const auto &email : subjects.emails)
    if (!email.trimmed().isEmpty()) labelFor(QStringLiteral("Email"), email.trimmed().toLower(), QString());
}

QString Pseudonymizer::labelFor(const QString &kind, const QString &key, const QString &surface) {
  const auto id = kind + QLatin1Char('\0') + key;
  auto it = mLabelByKey.find(id);
  if (it == mLabelByKey.end()) {
    const auto label = QStringLiteral("[%1 %2]").arg(kind).arg(++mCount[kind]);
    it = mLabelByKey.insert(id, label);
    mSurface.insert(normalisedLabel(label.mid(1, label.size() - 2)), QString());
  }
  // A restored label shows the spelling that was in the text.
  auto &known = mSurface[normalisedLabel(it->mid(1, it->size() - 2))];
  if (known.isEmpty()) known = surface;
  return *it;
}

QString Pseudonymizer::replaceNames(const QString &text) {
  QString out;
  qsizetype last = 0;
  const Person *previous = nullptr;
  qsizetype previousEnd = -1;
  auto it = wordPattern().globalMatch(text);
  while (it.hasNext()) {
    const auto match = it.next();
    const auto word = match.captured().toLower();
    const Person *person = nullptr;
    if (match.captured().front().isLetter()) {
      for (const Person *candidate : {&mClient, &mSpecialist}) {
        for (const auto &stem : candidate->stems)
          if (matchesStem(word, stem)) { person = candidate; break; }
        if (person) break;
      }
    }
    if (!person) continue;
    const auto key = normalisedLabel(person->label.mid(1, person->label.size() - 2));
    // Words of one person next to each other become a single label.
    const auto gap = text.mid(previousEnd, match.capturedStart() - previousEnd);
    if (previous == person && previousEnd >= 0 && gap.trimmed().isEmpty() && !gap.isEmpty()) {
      out += text.mid(last, previousEnd - last);
      last = match.capturedEnd();
      previousEnd = last;
      continue;
    }
    out += text.mid(last, match.capturedStart() - last) + person->label;
    if (!mSurface.contains(key)) mSurface.insert(key, match.captured());
    last = match.capturedEnd();
    previous = person;
    previousEnd = last;
  }
  return out + text.mid(last);
}

QString Pseudonymizer::apply(const QString &text) {
  QString result;
  qsizetype last = 0;
  auto emails = emailPattern().globalMatch(text);
  while (emails.hasNext()) {
    const auto m = emails.next();
    result += text.mid(last, m.capturedStart() - last) + labelFor(QStringLiteral("Email"), m.captured().toLower(), m.captured());
    last = m.capturedEnd();
  }
  result += text.mid(last);

  QString withPhones;
  last = 0;
  auto phones = phonePattern().globalMatch(result);
  while (phones.hasNext()) {
    const auto m = phones.next();
    const auto digits = digitsOf(m.captured());
    const bool separated = m.captured().startsWith(QLatin1Char('+')) || m.captured().contains(QRegularExpression(QStringLiteral(R"([\s\-().])")));
    const bool plausible = digits.size() >= 10 && digits.size() <= 15 &&
        (separated || (digits.size() == 11 && (digits[0] == QLatin1Char('7') || digits[0] == QLatin1Char('8'))) ||
         (digits.size() == 10 && digits[0] == QLatin1Char('9')));
    // 2026-10-29 style dates have 8 digits; only 10-15 digit strings are phones.
    if (!plausible) continue;
    withPhones += result.mid(last, m.capturedStart() - last) +
                  labelFor(QStringLiteral("Телефон"), digits.right(10), m.captured());
    last = m.capturedEnd();
  }
  withPhones += result.mid(last);
  return replaceNames(withPhones);
}

QByteArray Pseudonymizer::applyJson(const QByteArray &jsonPayload) {
  return apply(QString::fromUtf8(jsonPayload)).toUtf8();
}

QString Pseudonymizer::restoreText(const QString &text) const {
  QString out;
  qsizetype last = 0;
  auto it = labelPattern().globalMatch(text);
  while (it.hasNext()) {
    const auto m = it.next();
    const auto surface = mSurface.constFind(normalisedLabel(m.captured(1)));
    if (surface == mSurface.constEnd() || surface->isEmpty()) continue;
    out += text.mid(last, m.capturedStart() - last) + *surface;
    last = m.capturedEnd();
  }
  return out + text.mid(last);
}

QJsonValue Pseudonymizer::restore(const QJsonValue &reply) const {
  switch (reply.type()) {
  case QJsonValue::String: return restoreText(reply.toString());
  case QJsonValue::Array: {
    QJsonArray array;
    for (const auto &item : reply.toArray()) array.append(restore(item));
    return array;
  }
  case QJsonValue::Object: {
    QJsonObject object;
    const auto source = reply.toObject();
    for (auto it = source.begin(); it != source.end(); ++it) object.insert(it.key(), restore(it.value()));
    return object;
  }
  default: return reply;
  }
}

} // namespace pcm::prompter
