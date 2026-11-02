#include "pseudonymizer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <gtest/gtest.h>

using pcm::prompter::PseudonymSubjects;
using pcm::prompter::Pseudonymizer;

namespace {
PseudonymSubjects subjects() {
  PseudonymSubjects s;
  s.clientNames = {"Иван", "Иванов", "Петрович"};
  s.specialistNames = {"Мария", "Сидорова"};
  s.phones = {"+7 999 123-45-67"};
  s.emails = {"ivan@example.com"};
  return s;
}
} // namespace

TEST(Pseudonymizer, ReplacesNameInCases) {
  Pseudonymizer p(subjects());
  for (const char *form : {"Иван", "Ивана", "Ивану", "Иваном", "Иванова", "Ивановым", "Иванову"}) {
    const auto out = p.apply(QStringLiteral("Сегодня говорил с %1 долго").arg(QString::fromUtf8(form)));
    EXPECT_EQ(out, QStringLiteral("Сегодня говорил с [Клиент] долго")) << form;
  }
}

TEST(Pseudonymizer, FullNameCollapsesToOneLabel) {
  Pseudonymizer p(subjects());
  EXPECT_EQ(p.apply("Иван Петрович Иванов пришёл"), "[Клиент] пришёл");
}

TEST(Pseudonymizer, ReplacesSpecialistInCases) {
  Pseudonymizer p(subjects());
  EXPECT_EQ(p.apply("Мария сказала, что Марии надо, Марию видели, с Марией"),
            "[Специалист] сказала, что [Специалист] надо, [Специалист] видели, с [Специалист]");
}

TEST(Pseudonymizer, DoesNotMangleUnrelatedWords) {
  PseudonymSubjects s;
  s.clientNames = {"Ян", "Анна"};
  Pseudonymizer p(s);
  EXPECT_EQ(p.apply("Янтарь, Аннушка и аннотация"), "Янтарь, Аннушка и аннотация");
}

TEST(Pseudonymizer, PhoneNumbersFromTextAndSubjects) {
  Pseudonymizer p(subjects());
  EXPECT_EQ(p.apply("звони +7 (999) 123-45-67 или 89991234567"), "звони [Телефон 1] или [Телефон 1]");
  EXPECT_EQ(p.apply("ещё +7 912 000-11-22"), "ещё [Телефон 2]");
}

TEST(Pseudonymizer, DatesAndShortNumbersAreNotPhones) {
  Pseudonymizer p({});
  EXPECT_EQ(p.apply("встреча 2026-10-29 в 14:30, сеанс 12345"), "встреча 2026-10-29 в 14:30, сеанс 12345");
}

TEST(Pseudonymizer, EmailsReplaced) {
  Pseudonymizer p(subjects());
  EXPECT_EQ(p.apply("пишите на Ivan@Example.com и на other@mail.ru"), "пишите на [Email 1] и на [Email 2]");
}

TEST(Pseudonymizer, RestoreReturnsOriginalSurfaceForm) {
  Pseudonymizer p(subjects());
  const auto out = p.apply("Позвонить Ивану по +7 (999) 123-45-67");
  EXPECT_EQ(p.restoreText(out), "Позвонить Ивану по +7 (999) 123-45-67");
}

TEST(Pseudonymizer, RestoreIsRecursive) {
  Pseudonymizer p(subjects());
  p.apply("Иван");
  const QJsonObject reply{{"a", QJsonArray{QJsonObject{{"text", "Позвонить [Клиент]"}}}}, {"n", 3}};
  const auto restored = p.restore(QJsonValue(reply)).toObject();
  EXPECT_EQ(restored["a"].toArray()[0].toObject()["text"].toString(), "Позвонить Иван");
  EXPECT_EQ(restored["n"].toInt(), 3);
}

TEST(Pseudonymizer, RestoreToleratesCaseAndSpaceInLabel) {
  Pseudonymizer p(subjects());
  p.apply("Иван +7 999 123-45-67");
  EXPECT_EQ(p.restoreText("[клиент] и [ Телефон  1 ]"), "Иван и +7 999 123-45-67");
}

TEST(Pseudonymizer, UnknownLabelUntouched) {
  Pseudonymizer p(subjects());
  p.apply("Иван");
  EXPECT_EQ(p.restoreText("[Телефон 9] [Другое]"), "[Телефон 9] [Другое]");
}

TEST(Pseudonymizer, NoSubjectsNoHitsIsIdentity) {
  Pseudonymizer p({});
  EXPECT_EQ(p.apply("Просто текст про сон."), "Просто текст про сон.");
}

TEST(Pseudonymizer, JsonPayloadKeepsFormattingAndSkipsEscapes) {
  Pseudonymizer p(subjects());
  const QByteArray in = R"({"utterances": [{"id": "u1", "speaker": "Иван", "text": "привет\nИван, Ивану звонил"}]})";
  const auto out = p.applyJson(in);
  EXPECT_EQ(out, QByteArray(R"({"utterances": [{"id": "u1", "speaker": "[Клиент]", "text": "привет\n[Клиент], [Клиент] звонил"}]})"));
}

TEST(Pseudonymizer, SameInstanceNumbersConsistentlyAcrossCalls) {
  Pseudonymizer p(subjects());
  // ivan@example.com from the subjects already holds [Email 1]
  EXPECT_EQ(p.apply("a@b.ru"), "[Email 2]");
  EXPECT_EQ(p.apply("c@d.ru a@b.ru"), "[Email 3] [Email 2]");
}
