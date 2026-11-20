#include <gtest/gtest.h>

#include <QDateTime>
#include <QTimeZone>

#include "change_log_text.h"
#include "schema.hpp"

namespace {

DuckEventChangeLog statusChange(const int64_t oldStatId, const int64_t newStatId,
                                 const std::optional<std::string> &reason = std::nullopt) {
  DuckEventChangeLog entry;
  entry.change_kind = 1;
  entry.old_event_stat_id = oldStatId;
  entry.new_event_stat_id = newStatId;
  entry.cancellation_reason = reason;
  return entry;
}

DuckEventChangeLog paymentChange(const int64_t oldPaymentStatId, const int64_t newPaymentStatId) {
  DuckEventChangeLog entry;
  entry.change_kind = 2;
  entry.old_payment_stat_id = oldPaymentStatId;
  entry.new_payment_stat_id = newPaymentStatId;
  return entry;
}

} // namespace

TEST(ChangeLogTextTest, EventStatusLabelsCoverAllSixCases) {
  // old_event_stat_id fixed at 1 (Scheduled) so the "old" side of the arrow
  // is stable while we sweep the "new" side through every known status id.
  EXPECT_EQ(pcm::client_notes::changeLogLineText(statusChange(1, 1)),
            QStringLiteral("Status changed: Scheduled → Scheduled"));
  EXPECT_EQ(pcm::client_notes::changeLogLineText(statusChange(1, 2)),
            QStringLiteral("Status changed: Scheduled → Completed"));
  EXPECT_EQ(pcm::client_notes::changeLogLineText(statusChange(1, 3)),
            QStringLiteral("Status changed: Scheduled → Canceled"));
  EXPECT_EQ(pcm::client_notes::changeLogLineText(statusChange(1, 4)),
            QStringLiteral("Status changed: Scheduled → Confirmed"));
  EXPECT_EQ(pcm::client_notes::changeLogLineText(statusChange(1, 5)),
            QStringLiteral("Status changed: Scheduled → No show"));
  EXPECT_EQ(pcm::client_notes::changeLogLineText(statusChange(1, 6)),
            QStringLiteral("Status changed: Scheduled → Rescheduled"));
}

TEST(ChangeLogTextTest, PaymentStatusLabelsCoverAllFiveCases) {
  // old_payment_stat_id fixed at 1 (Pending) so the "old" side of the arrow
  // is stable while we sweep the "new" side through every known payment
  // status id.
  EXPECT_EQ(pcm::client_notes::changeLogLineText(paymentChange(1, 1)),
            QStringLiteral("Payment status changed: Pending → Pending"));
  EXPECT_EQ(pcm::client_notes::changeLogLineText(paymentChange(1, 2)),
            QStringLiteral("Payment status changed: Pending → Paid"));
  EXPECT_EQ(pcm::client_notes::changeLogLineText(paymentChange(1, 3)),
            QStringLiteral("Payment status changed: Pending → Canceled"));
  EXPECT_EQ(pcm::client_notes::changeLogLineText(paymentChange(1, 4)),
            QStringLiteral("Payment status changed: Pending → Refunded"));
  EXPECT_EQ(pcm::client_notes::changeLogLineText(paymentChange(1, 5)),
            QStringLiteral("Payment status changed: Pending → Skipped"));
}

TEST(ChangeLogTextTest, CanceledStatusFoldsCancellationReason) {
  const auto text = pcm::client_notes::changeLogLineText(statusChange(1, 3, std::string{"No availability"}));
  EXPECT_EQ(text, QStringLiteral("Status changed: Scheduled → Canceled (No availability)"));
}

TEST(ChangeLogTextTest, NoShowStatusFoldsCancellationReason) {
  const auto text = pcm::client_notes::changeLogLineText(statusChange(1, 5, std::string{"Client did not show"}));
  EXPECT_EQ(text, QStringLiteral("Status changed: Scheduled → No show (Client did not show)"));
}

TEST(ChangeLogTextTest, StatusChangeWithoutReasonHasNoParenthetical) {
  const auto text = pcm::client_notes::changeLogLineText(statusChange(1, 3));
  EXPECT_EQ(text, QStringLiteral("Status changed: Scheduled → Canceled"));
  EXPECT_FALSE(text.contains('('));
}

TEST(ChangeLogTextTest, RescheduleFormatsOldAndNewDates) {
  const auto oldAt = QDateTime(QDate(2026, 3, 10), QTime(9, 0), QTimeZone(QTimeZone::LocalTime));
  const auto newAt = QDateTime(QDate(2026, 3, 12), QTime(14, 30), QTimeZone(QTimeZone::LocalTime));

  DuckEventChangeLog entry;
  entry.change_kind = 3;
  entry.old_start_date = oldAt.toMSecsSinceEpoch();
  entry.new_start_date = newAt.toMSecsSinceEpoch();

  const auto expected = QStringLiteral("Rescheduled from %1 to %2")
                             .arg(oldAt.toString("dd.MM.yyyy HH:mm"), newAt.toString("dd.MM.yyyy HH:mm"));
  EXPECT_EQ(pcm::client_notes::changeLogLineText(entry), expected);
}

TEST(ChangeLogTextTest, UnknownChangeKindReturnsEmptyString) {
  DuckEventChangeLog entry;
  entry.change_kind = 99;
  EXPECT_TRUE(pcm::client_notes::changeLogLineText(entry).isEmpty());
}
