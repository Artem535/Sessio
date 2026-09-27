#include "calls_page.h"
#include "token_backend_client.h"
#include "fake_token_backend_server.h"

#include <QApplication>
#include <QPushButton>
#include <QLineEdit>
#include <QTest>
#include <gtest/gtest.h>

// CallsPage is exercised against a real TokenBackendClient talking to a fake
// local HTTP server (same double as Task 4), rather than a hand-rolled fake
// TokenBackendClient — TokenBackendClient is a concrete, final class with no
// virtual seam, and the server-side double is already proven reliable.
TEST(CallsPageTest, JoiningByCodeSwitchesToCallPageOnceTokenArrives) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, R"({
    "endpointUrl": "wss://livekit.example.test", "roomName": "room-1",
    "token": "jwt-1", "expiresAt": 999
  })");
  pcm::tokenclient::TokenBackendClient client(server.baseUrl().toString());
  pcm::video::DeviceManager deviceManager;
  CallsPage page(/*specialistMode=*/false, &deviceManager, &client);

  auto *codeEdit = page.findChild<QLineEdit *>("joinCodeEdit");
  auto *passcodeEdit = page.findChild<QLineEdit *>("joinPasscodeEdit");
  auto *connectButton = page.findChild<QPushButton *>("joinByCodeButton");
  ASSERT_NE(codeEdit, nullptr);
  ASSERT_NE(passcodeEdit, nullptr);
  ASSERT_NE(connectButton, nullptr);
  codeEdit->setText("code-1");
  passcodeEdit->setText("123456");
  connectButton->click();

  // Token request is asynchronous; pump the event loop until CallPage appears.
  QTRY_VERIFY_WITH_TIMEOUT(page.findChild<QWidget *>("deviceCheckWidget") != nullptr, 2000);
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
