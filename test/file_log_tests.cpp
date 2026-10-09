#include "file_log.h"

#include <QApplication>
#include <QComboBox>
#include <QAbstractItemView>
#include <QFile>
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "combo_fit.h"

Q_LOGGING_CATEGORY(logFileLogTest, "pcm.test")

namespace {
QByteArray readAll(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

// One process, one handler: install once and check writing, then rotation.
TEST(FileLogTest, WritesMessagesWithCategoryAndRotatesPastTheLimit) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  pcm::app::installFileLog(dir.filePath("logs"), 300);
  const QString path = pcm::app::fileLogPath();
  ASSERT_FALSE(path.isEmpty());

  qCInfo(logFileLogTest) << "teardown: room closed";
  const QByteArray first = readAll(path);
  EXPECT_TRUE(first.contains("info pcm.test: teardown: room closed")) << first.constData();

  for (int i = 0; i < 20; ++i) qCWarning(logFileLogTest) << "line" << i;
  EXPECT_TRUE(QFile::exists(path + ".1"));
  EXPECT_LE(QFile(path).size(), 300 + 200);
  EXPECT_TRUE(readAll(path).contains("line 19"));
}

TEST(ComboFitTest, DropDownGrowsToTheLongestDeviceNameAndEachEntryHasItsName) {
  QComboBox combo;
  combo.addItem("USB");
  const QString longName = QStringLiteral("Динамики (Realtek(R) High Definition Audio with a very long name)");
  combo.addItem(longName);
  combo.resize(120, 30);

  pcm::widgets::fitComboToLongItems(&combo);

  EXPECT_GE(combo.view()->minimumWidth(), combo.view()->fontMetrics().horizontalAdvance(longName));
  EXPECT_EQ(combo.itemData(1, Qt::ToolTipRole).toString(), longName);
  combo.setCurrentIndex(1);
  EXPECT_EQ(combo.toolTip(), longName);
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
