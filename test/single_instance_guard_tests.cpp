#include "single_instance_guard.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <gtest/gtest.h>

TEST(SingleInstanceGuardTest, FirstInstanceIsPrimary) {
  SingleInstanceGuard first;
  EXPECT_TRUE(first.isPrimaryInstance());
}

TEST(SingleInstanceGuardTest, SecondInstanceForwardsUrlToFirst) {
  SingleInstanceGuard first;
  ASSERT_TRUE(first.isPrimaryInstance());
  QSignalSpy urlSpy(&first, &SingleInstanceGuard::urlReceivedFromSecondaryInstance);

  SingleInstanceGuard second;
  EXPECT_FALSE(second.isPrimaryInstance());
  second.forwardToPrimaryInstance("sessio://join?code=abc&passcode=123456");

  ASSERT_TRUE(urlSpy.wait(2000));
  EXPECT_EQ(urlSpy.at(0).at(0).toString(), QStringLiteral("sessio://join?code=abc&passcode=123456"));
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
