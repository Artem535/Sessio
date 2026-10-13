#include "schedule_zoneinfo.h"

#include <gtest/gtest.h>
#include <schedule/schedule.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace {
using namespace pcm::meeting;

// A directory shaped like libical's zoneinfo, holding only the marker zone.
void makeZoneData(const QString &dir) {
  ASSERT_TRUE(QDir().mkpath(dir + "/America"));
  QFile marker(dir + "/America/New_York.ics");
  ASSERT_TRUE(marker.open(QIODevice::WriteOnly));
  marker.write("BEGIN:VCALENDAR\nEND:VCALENDAR\n");
}

pcm::schedule::Snapshot weeklyMoscow() {
  pcm::schedule::Snapshot s;
  s.revision = 1;
  s.timezone = "Europe/Moscow";
  s.dtstartLocal = "2026-10-06T18:00:00";
  s.durationSeconds = 3600;
  s.rrule = "FREQ=WEEKLY;INTERVAL=1;BYDAY=TU";
  s.active = s.joinEnabled = true;
  return s;
}

TEST(ScheduleZoneinfoTest, InstalledLayoutsAreFoundRelativeToTheExecutable) {
  for (const QString layout : {"share/sessio/zoneinfo", "zoneinfo"}) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const auto app = root.path() + "/bin";
    ASSERT_TRUE(QDir().mkpath(app));
    const auto data = (layout == "zoneinfo" ? app : root.path()) + "/" + layout;
    makeZoneData(data);
    // bin/../share/sessio/zoneinfo (RPM, AppImage, cmake --install) and
    // bin/zoneinfo (Windows installer) both resolve.
    const auto expected = QDir::cleanPath(data);
    EXPECT_EQ(resolveZoneinfoDirectory(app, {}), expected) << layout.toStdString();
  }
  QTemporaryDir bundle; // macOS: Contents/MacOS/.. /Resources/zoneinfo
  ASSERT_TRUE(QDir().mkpath(bundle.path() + "/Contents/MacOS"));
  makeZoneData(bundle.path() + "/Contents/Resources/zoneinfo");
  EXPECT_EQ(resolveZoneinfoDirectory(bundle.path() + "/Contents/MacOS", {}),
            QDir::cleanPath(bundle.path() + "/Contents/Resources/zoneinfo"));
}

TEST(ScheduleZoneinfoTest, EnvironmentOverrideWinsAndEmptyDirectoriesAreSkipped) {
  QTemporaryDir root;
  ASSERT_TRUE(root.isValid());
  ASSERT_TRUE(QDir().mkpath(root.path() + "/bin"));
  ASSERT_TRUE(QDir().mkpath(root.path() + "/share/sessio/zoneinfo")); // present but empty
  EXPECT_TRUE(resolveZoneinfoDirectory(root.path() + "/bin", {}).isEmpty());
  makeZoneData(root.path() + "/override");
  EXPECT_EQ(resolveZoneinfoDirectory(root.path() + "/bin", root.path() + "/override"),
            QDir::cleanPath(root.path() + "/override"));
}

TEST(ScheduleZoneinfoTest, MissingDataLeavesNamedZonesRejectedInsteadOfPublishing) {
  const auto original = pcm::schedule::zoneinfoDirectory();
  QTemporaryDir root;
  ASSERT_TRUE(root.isValid());
  // Pretend an installed layout whose data is missing and no compiled-in data.
  pcm::schedule::setZoneinfoDirectory((root.path() + "/none").toStdString());
  EXPECT_FALSE(configureScheduleZoneinfo(root.path() + "/bin"));
  const auto validation = pcm::schedule::validate(weeklyMoscow());
  EXPECT_FALSE(validation.valid);
  EXPECT_EQ(validation.error, "unknown timezone");
  pcm::schedule::setZoneinfoDirectory(original);
}

TEST(ScheduleZoneinfoTest, ConfiguredDirectoryIsUsedByValidation) {
  const auto original = pcm::schedule::zoneinfoDirectory();
  ASSERT_TRUE(pcm::schedule::zoneinfoDirectoryHasData(original));
  QTemporaryDir root;
  ASSERT_TRUE(root.isValid());
  ASSERT_TRUE(QDir().mkpath(root.path() + "/bin"));
  // Copy only a marker so the resolver accepts it, then check the schedule
  // engine really switched to that directory: Moscow is not in it.
  makeZoneData(root.path() + "/share/sessio/zoneinfo");
  EXPECT_TRUE(configureScheduleZoneinfo(root.path() + "/bin"));
  EXPECT_EQ(pcm::schedule::zoneinfoDirectory(),
            QDir::cleanPath(root.path() + "/share/sessio/zoneinfo").toStdString());
  EXPECT_FALSE(pcm::schedule::validate(weeklyMoscow()).valid);
  pcm::schedule::setZoneinfoDirectory(original);
  EXPECT_TRUE(pcm::schedule::validate(weeklyMoscow()).valid);
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
