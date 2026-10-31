#include "series_timezone_dialog.h"

#include <QApplication>
#include <QLabel>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <gtest/gtest.h>

using pcm::eventpage::SeriesTimezoneDialog;

namespace {

QDateTime utc(const char *iso) { return QDateTime::fromString(QString::fromLatin1(iso), Qt::ISODate); }

// Today's calendar and two candidate zones, as the shared module would report
// them for a Tuesday 19:00 Moscow series.
SeriesTimezoneDialog::PreviewFn fakePreview() {
  return [](const std::string &timezone) -> std::optional<QVector<QDateTime>> {
    if (timezone.empty() || timezone == "Europe/Moscow") {
      return QVector<QDateTime>{utc("2026-10-20T16:00:00Z"), utc("2026-10-27T16:00:00Z")};
    }
    if (timezone == "Asia/Tokyo") {
      return QVector<QDateTime>{utc("2026-10-26T16:00:00Z"), utc("2026-11-02T16:00:00Z")};
    }
    return std::nullopt;
  };
}

} // namespace

TEST(SeriesTimezoneDialogTest, ConfirmingIsOnlyAllowedWhenNoDateMoves) {
  SeriesTimezoneDialog dialog(fakePreview(), "Europe/Moscow");
  EXPECT_EQ(dialog.selectedTimezone(), "Europe/Moscow");
  EXPECT_TRUE(dialog.canConfirm());
  EXPECT_TRUE(dialog.findChild<QLabel *>("seriesTimezoneWarning")->text().isEmpty());
  EXPECT_TRUE(dialog.findChild<QLabel *>("seriesTimezoneNewDates")->text().contains("Europe/Moscow"));

  dialog.setSelectedTimezone("Asia/Tokyo");
  EXPECT_FALSE(dialog.canConfirm());
  EXPECT_TRUE(dialog.findChild<QLabel *>("seriesTimezoneWarning")->text().contains("would move"));
  // The dates the series has today stay visible next to the candidate's.
  EXPECT_TRUE(dialog.findChild<QLabel *>("seriesTimezoneCurrentDates")->text().contains("Dates today"));
}

TEST(SeriesTimezoneDialogTest, NoTimezoneIsGuessedWhenNoneWasProposed) {
  SeriesTimezoneDialog dialog(fakePreview(), "");
  EXPECT_TRUE(dialog.selectedTimezone().isEmpty());
  EXPECT_FALSE(dialog.canConfirm());
}

TEST(SeriesTimezoneDialogTest, UnusableTimezoneCannotBeConfirmed) {
  SeriesTimezoneDialog dialog(fakePreview(), "Europe/Moscow");
  dialog.setSelectedTimezone("Europe/Berlin"); // listed by Qt, refused by the preview
  EXPECT_FALSE(dialog.canConfirm());
  EXPECT_TRUE(dialog.findChild<QLabel *>("seriesTimezoneWarning")->text().contains("cannot be used"));
}

int main(int argc, char **argv) {
  QTemporaryDir isolatedHome;
  qputenv("XDG_CONFIG_HOME", isolatedHome.path().toUtf8());
  qputenv("HOME", isolatedHome.path().toUtf8());
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QStandardPaths::setTestModeEnabled(true);
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
