#include "client_mode_window.h"
#include "token_backend_client.h"

#include <QApplication>
#include <QLineEdit>
#include <gtest/gtest.h>

TEST(ClientModeWindowTest, HostsCallsPageWithNoOwnMeetingsList) {
  pcm::video::DeviceManager deviceManager;
  pcm::tokenclient::TokenBackendClient tokenClient("http://127.0.0.1:1");
  ClientModeWindow window(&deviceManager, &tokenClient);

  EXPECT_NE(window.findChild<QLineEdit *>("joinCodeEdit"), nullptr);
  EXPECT_EQ(window.findChild<QWidget *>("ownMeetingsList"), nullptr);
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
