#include "accent_color.h"
#include "month_picker_widget.h"
#include "rounded_calendar_widget.h"

#include <oclero/qlementine/style/QlementineStyle.hpp>

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {
const QString kSourceDir = QStringLiteral(SESSIO_SOURCE_DIR);
const QString kThemePath = kSourceDir + "/resources/themes/qlementine_dark.json";
const QColor kLogoPurple(0xa7, 0x8b, 0xfa);  // fill of resources/icons/brain-solid-full.svg

QJsonObject loadTheme() {
  QFile file(kThemePath);
  EXPECT_TRUE(file.open(QIODevice::ReadOnly));
  return QJsonDocument::fromJson(file.readAll()).object();
}

int countPixels(const QImage &image, const QColor &color) {
  int n = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if (QColor(image.pixel(x, y)).rgb() == color.rgb()) {
        ++n;
      }
    }
  }
  return n;
}
}  // namespace

TEST(AccentColorTest, ThemeAccentIsTheLogoPurpleWithItsVariants) {
  const auto theme = loadTheme();
  EXPECT_EQ(theme["primaryColor"].toString(), "#a78bfa");
  EXPECT_EQ(QColor(theme["primaryColor"].toString()), kLogoPurple);
  EXPECT_EQ(theme["primaryColorHovered"].toString(), "#b9a2fb");
  EXPECT_EQ(theme["primaryColorPressed"].toString(), "#c8b6fc");
  EXPECT_EQ(theme["primaryColorDisabled"].toString(), "#37344d");
  EXPECT_EQ(theme["focusColor"].toString(), "#a78bfa6a");
}

TEST(AccentColorTest, LogoSvgFillMatchesThemeAccent) {
  QFile svg(kSourceDir + "/resources/icons/brain-solid-full.svg");
  ASSERT_TRUE(svg.open(QIODevice::ReadOnly));
  const auto match = QRegularExpression("fill=\"(#[0-9A-Fa-f]{6})\"")
                         .match(QString::fromUtf8(svg.readAll()));
  ASSERT_TRUE(match.hasMatch());
  EXPECT_EQ(QColor(match.captured(1)), QColor(loadTheme()["primaryColor"].toString()));
}

TEST(AccentColorTest, TextOnAccentIsReadable) {
  const auto theme = loadTheme();
  const auto lum = [](const QColor &c) {
    const auto lin = [](qreal v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
  };
  for (const auto *pair : {"primaryColor", "primaryColorHovered", "primaryColorPressed"}) {
    const double a = lum(QColor(theme[pair].toString()));
    const double b = lum(QColor(theme["primaryColorForeground"].toString()));
    EXPECT_GE((std::max(a, b) + 0.05) / (std::min(a, b) + 0.05), 4.5) << pair;
  }
}

TEST(AccentColorTest, HelperReadsTheQlementinePaletteHighlight) {
  auto *style = qobject_cast<oclero::qlementine::QlementineStyle *>(QApplication::style());
  ASSERT_NE(style, nullptr);
  style->setThemeJsonPath(kThemePath);
  EXPECT_EQ(pcm::widgets::accentColor(), kLogoPurple);
  EXPECT_EQ(pcm::widgets::accentColor(), style->theme().primaryColor);
  EXPECT_EQ(pcm::widgets::onAccentColor(), QColor(0x1b, 0x17, 0x30));
  const QColor soft = pcm::widgets::accentSoftColor();
  EXPECT_GT(soft.lightness(), pcm::widgets::accentColor().lightness());
  EXPECT_EQ(pcm::widgets::cssRgba(kLogoPurple, 0.5), "rgba(167, 139, 250, 0.50)");

  QPalette custom;
  custom.setColor(QPalette::Highlight, QColor(10, 20, 30));
  EXPECT_EQ(pcm::widgets::accentColor(custom), QColor(10, 20, 30));
}

TEST(AccentColorTest, SelectedDayPillAndMonthTileArePaintedInTheAccent) {
  auto *style = qobject_cast<oclero::qlementine::QlementineStyle *>(QApplication::style());
  ASSERT_NE(style, nullptr);
  style->setThemeJsonPath(kThemePath);

  RoundedCalendarWidget calendar;
  calendar.resize(360, 320);
  calendar.setSelectedDate(QDate::currentDate());
  const auto day = calendar.grab().toImage();
  EXPECT_GT(countPixels(day, kLogoPurple), 200);
  EXPECT_EQ(countPixels(day, QColor(93, 123, 230)), 0);

  MonthPickerWidget picker;
  picker.resize(360, 320);
  picker.setDisplayedMonth(QDate::currentDate());
  const auto month = picker.grab().toImage();
  EXPECT_GT(countPixels(month, kLogoPurple), 200);
  EXPECT_EQ(countPixels(month, QColor(93, 123, 230)), 0);
}

TEST(AccentColorTest, NoHardCodedBlueAccentLiteralsRemainInSources) {
  const QRegularExpression blue(
      "0x5086ff|#5086ff|93,\\s*123,\\s*230|0x9fc0ff|0x9f,\\s*0xc0,\\s*0xff|0xd9e6ff|"
      "0xd9,\\s*0xe6,\\s*0xff|76,\\s*132,\\s*255|98,\\s*148,\\s*255|#4f83ff|120,\\s*170,\\s*255",
      QRegularExpression::CaseInsensitiveOption);
  QDirIterator it(kSourceDir + "/src", {"*.cpp", "*.h", "*.hpp", "*.ui"}, QDir::Files,
                  QDirIterator::Subdirectories);
  QStringList offenders;
  while (it.hasNext()) {
    QFile file(it.next());
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    if (blue.match(QString::fromUtf8(file.readAll())).hasMatch()) {
      offenders << file.fileName();
    }
  }
  EXPECT_TRUE(offenders.isEmpty()) << offenders.join(", ").toStdString();
}

int main(int argc, char **argv) {
  QTemporaryDir home;
  qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
  qputenv("HOME", home.path().toUtf8());
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("SessioAccentColorTests");
  app.setStyle(new oclero::qlementine::QlementineStyle(&app));
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
