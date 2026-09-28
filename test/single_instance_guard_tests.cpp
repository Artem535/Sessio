#include "single_instance_guard.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <gtest/gtest.h>

TEST(SingleInstanceGuardTest, FirstInstanceIsPrimary) {
  SingleInstanceGuard first;
  EXPECT_TRUE(first.isPrimaryInstance());
}

TEST(SingleInstanceGuardTest, SecondInstanceIsNotPrimary) {
  // Regression coverage for the QLockFile-based redesign: exactly one of two
  // guards constructed in the same process must report itself as primary.
  // This is the property the old listen()-only design could not guarantee
  // on Windows, where QLocalServer::listen() can succeed for more than one
  // process under the same pipe name.
  SingleInstanceGuard first;
  ASSERT_TRUE(first.isPrimaryInstance());

  SingleInstanceGuard second;
  EXPECT_FALSE(second.isPrimaryInstance());
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

TEST(SingleInstanceGuardTest, SecondInstanceForwardsPlainActivationWithNoUrl) {
  // Regression test for the "plain relaunch, no sessio:// link" bug: a
  // second launch with an empty launchUrl must still reach the primary via
  // urlReceivedFromSecondaryInstance (so Application::handleJoinLink() can
  // raise the window), not be silently dropped. forwardToPrimaryInstance()
  // substitutes a non-empty activation sentinel for an empty url internally,
  // since writing zero bytes over the local socket never triggers the
  // peer's readyRead() at all.
  SingleInstanceGuard first;
  ASSERT_TRUE(first.isPrimaryInstance());
  QSignalSpy urlSpy(&first, &SingleInstanceGuard::urlReceivedFromSecondaryInstance);

  SingleInstanceGuard second;
  EXPECT_FALSE(second.isPrimaryInstance());
  second.forwardToPrimaryInstance(QString());

  ASSERT_TRUE(urlSpy.wait(2000));
  const auto received = urlSpy.at(0).at(0).toString();
  EXPECT_FALSE(received.isEmpty());
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
