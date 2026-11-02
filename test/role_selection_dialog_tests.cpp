#include "role_selection_dialog.h"

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QTest>
#include <cstdlib>
#include <gtest/gtest.h>

TEST(RoleSelectionDialogTest, ClickingSpecialistAcceptsWithSpecialistRole) {
  RoleSelectionDialog dialog;
  auto *specialistButton = dialog.findChild<QPushButton *>("specialistButton");
  ASSERT_NE(specialistButton, nullptr);

  QTest::mouseClick(specialistButton, Qt::LeftButton);

  ASSERT_TRUE(dialog.selectedRole().has_value());
  EXPECT_EQ(*dialog.selectedRole(), pcm::config::AppRole::Specialist);
  EXPECT_EQ(dialog.result(), QDialog::Accepted);
}

TEST(RoleSelectionDialogTest, ClickingClientAcceptsWithClientRole) {
  RoleSelectionDialog dialog;
  auto *clientButton = dialog.findChild<QPushButton *>("clientButton");
  ASSERT_NE(clientButton, nullptr);

  QTest::mouseClick(clientButton, Qt::LeftButton);

  ASSERT_TRUE(dialog.selectedRole().has_value());
  EXPECT_EQ(*dialog.selectedRole(), pcm::config::AppRole::Client);
}

TEST(RoleSelectionDialogTest, NoRoleSelectedBeforeAnyClick) {
  RoleSelectionDialog dialog;
  EXPECT_FALSE(dialog.selectedRole().has_value());
}

TEST(RoleSelectionDialogTest, HasFixedNonResizableSize) {
  RoleSelectionDialog dialog;
  EXPECT_EQ(dialog.minimumSize(), dialog.maximumSize());
  EXPECT_GT(dialog.width(), 0);
  EXPECT_LE(dialog.width(), 480);
  EXPECT_LE(dialog.height(), 260);
  EXPECT_FALSE(dialog.windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
  EXPECT_TRUE(dialog.isModal());
}

TEST(RoleSelectionDialogTest, PromptIsCenteredHorizontally) {
  RoleSelectionDialog dialog;
  auto *prompt = dialog.findChild<QLabel *>("promptLabel");
  ASSERT_NE(prompt, nullptr);
  EXPECT_TRUE(prompt->alignment().testFlag(Qt::AlignHCenter));
}

TEST(RoleSelectionDialogTest, ButtonsAreCenteredAndWidthLimited) {
  RoleSelectionDialog dialog;
  dialog.show();
  QApplication::processEvents();
  for (const char *name : {"specialistButton", "clientButton"}) {
    auto *button = dialog.findChild<QPushButton *>(name);
    ASSERT_NE(button, nullptr);
    EXPECT_LE(button->maximumWidth(), 300) << name;
    EXPECT_LT(button->width(), dialog.width() - 60) << name;
    const int left = button->geometry().left();
    const int right = dialog.width() - 1 - button->geometry().right();
    EXPECT_LE(std::abs(left - right), 1) << name;
  }
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
