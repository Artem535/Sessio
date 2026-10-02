#include "role_selection_dialog.h"

#include <QApplication>
#include <QPushButton>
#include <QTest>
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

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
