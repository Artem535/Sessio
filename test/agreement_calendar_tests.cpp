#include "agreement_calendar.h"

#include <QDateTime>
#include <QJsonObject>
#include <gtest/gtest.h>

namespace {
QJsonObject record(const QString &date = "2026-11-05", const QString &time = "15:00") {
  return {{"id", "r1"}, {"text", "Встреча в четверг в 15:00"}, {"meeting", true}, {"date", date}, {"time", time}};
}
DuckEvent callEvent() {
  DuckEvent event;
  event.name = "Сессия с Анной";
  event.start_date = QDateTime(QDate(2026, 10, 29), QTime(10, 30)).toMSecsSinceEpoch();
  event.end_date = QDateTime(QDate(2026, 10, 29), QTime(11, 20)).toMSecsSinceEpoch();
  return event;
}
} // namespace

TEST(AgreementCalendar, TemplateUsesAgreementDateAndTime) {
  const auto t = agreementEventTemplate(record(), callEvent(), 7, "Анна", QDate(2026, 10, 29));
  EXPECT_EQ(t.date, QDate(2026, 11, 5));
  ASSERT_TRUE(t.time.has_value());
  EXPECT_EQ(*t.time, QTime(15, 0));
  EXPECT_EQ(t.durationMinutes, 50);
  EXPECT_EQ(t.title, "Сессия с Анной");
  EXPECT_EQ(t.notes, "Встреча в четверг в 15:00");
  EXPECT_EQ(t.clientId, 7);
}

TEST(AgreementCalendar, TemplateFallsBackToCallEventAndWeekLater) {
  const auto t = agreementEventTemplate(record("", ""), callEvent(), 7, "Анна", QDate(2026, 10, 29));
  EXPECT_EQ(t.date, QDate(2026, 11, 5));
  ASSERT_TRUE(t.time.has_value());
  EXPECT_EQ(*t.time, QTime(10, 30));
}

TEST(AgreementCalendar, TemplateWithoutClientOrEventUsesAgreementText) {
  const auto t = agreementEventTemplate(record("2026-11-02", ""), std::nullopt, std::nullopt, {}, QDate(2026, 10, 29));
  EXPECT_EQ(t.date, QDate(2026, 11, 2));
  EXPECT_FALSE(t.time.has_value());
  EXPECT_FALSE(t.durationMinutes.has_value());
  EXPECT_FALSE(t.clientId.has_value());
  EXPECT_EQ(t.title, "Встреча в четверг в 15:00");
  EXPECT_EQ(agreementEventTemplate(record(), std::nullopt, 3, "Анна", QDate(2026, 10, 29)).title, "Анна");
}

TEST(AgreementCalendar, InvalidStoredDateOrTimeIsIgnored) {
  const auto t = agreementEventTemplate(record("tomorrow", "25:99"), std::nullopt, std::nullopt, {}, QDate(2026, 10, 29));
  EXPECT_EQ(t.date, QDate(2026, 11, 5));
  EXPECT_FALSE(t.time.has_value());
}
