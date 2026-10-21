#include "notes_widget_frame.h"
#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <gtest/gtest.h>

TEST(NotesWidgetFrameTest, ShowsTitleStatusChipAndLabelledActions) {
  NotesWidgetFrame frame(QStringLiteral("AI summary"));
  EXPECT_EQ(frame.findChild<QLabel *>("notesWidgetTitle")->text(), "AI summary");
  auto *chip = frame.findChild<QLabel *>("notesWidgetStatus");
  EXPECT_TRUE(chip->isHidden());
  frame.setStatus(QStringLiteral("ready"));
  EXPECT_FALSE(chip->isHidden()); EXPECT_EQ(chip->text(), "ready");
  EXPECT_EQ(frame.findChild<QToolButton *>("notesWidgetMore"), nullptr);
  auto *edit = frame.addAction(QStringLiteral("Edit"), "aiSummaryEdit");
  EXPECT_EQ(edit->text(), "Edit");
  auto *remove = frame.addMenuAction(QStringLiteral("Delete"), "aiSummaryDelete");
  auto *more = frame.findChild<QToolButton *>("notesWidgetMore");
  ASSERT_NE(more, nullptr);
  EXPECT_FALSE(more->accessibleName().isEmpty());
  EXPECT_TRUE(more->menu()->actions().contains(remove));
  frame.setStatus({}); EXPECT_TRUE(chip->isHidden());
}

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  app.setStyle(new oclero::qlementine::QlementineStyle(&app));
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
