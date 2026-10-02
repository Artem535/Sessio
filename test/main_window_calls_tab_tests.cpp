#include "main_window.h"
#include "token_backend_client.h"

#include <QApplication>
#include <QPushButton>
#include <gtest/gtest.h>

TEST(MainWindowCallsTabTest, AddCallsPageAddsCallsTabAndPage) {
  MainWindow window;
  pcm::video::DeviceManager deviceManager;
  pcm::tokenclient::TokenBackendClient tokenClient("http://127.0.0.1:1");

  window.addCallsPage(&deviceManager, &tokenClient, [] { return QString(); });

  EXPECT_NE(window.getPage(MainWindow::Pages::calls), nullptr);
  EXPECT_NE(window.findChild<QPushButton *>(), nullptr); // sidebar buttons exist
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
