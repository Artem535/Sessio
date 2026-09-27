#include "calls_page.h"
#include "token_backend_client.h"
#include "fake_token_backend_server.h"
#include "fake_video_provider.h"

#include <QApplication>
#include <QPushButton>
#include <QLineEdit>
#include <QTest>
#include <gtest/gtest.h>

using pcm::video::test::FakeVideoProvider;

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

  // Fix round 1 self-review finding: CallPage's own DeviceCheckWidget child
  // ("deviceCheckWidget") exists from CallPage's construction — i.e. from
  // the moment the `CallsPage page(...)` line above returns, well before any
  // token round-trip. Waiting on findChild("deviceCheckWidget") alone (as
  // this test originally did) is therefore trivially satisfied immediately
  // and doesn't actually prove the click->token->attachSession sequence ran.
  // Substituting a FakeVideoProvider and waiting for CallsPage::startJoin()
  // to have actually constructed it is what makes this assertion meaningful
  // — see RealJoinIsGatedBehindDeviceCheckConfirmation below for why a fake
  // is used instead of the real LiveKitVideoProvider.
  FakeVideoProvider *fakeProvider = nullptr;
  page.setVideoProviderFactoryForTesting([&fakeProvider]() -> pcm::video::VideoProvider * {
    fakeProvider = new FakeVideoProvider();
    return fakeProvider;
  });

  auto *codeEdit = page.findChild<QLineEdit *>("joinCodeEdit");
  auto *passcodeEdit = page.findChild<QLineEdit *>("joinPasscodeEdit");
  auto *connectButton = page.findChild<QPushButton *>("joinByCodeButton");
  ASSERT_NE(codeEdit, nullptr);
  ASSERT_NE(passcodeEdit, nullptr);
  ASSERT_NE(connectButton, nullptr);
  codeEdit->setText("code-1");
  passcodeEdit->setText("123456");
  connectButton->click();

  // Token request is asynchronous; pump the event loop until CallsPage's
  // startJoin() has actually run (proven by the fake provider having been
  // constructed), at which point CallPage is showing and its device-check
  // screen is present.
  QTRY_VERIFY_WITH_TIMEOUT(fakeProvider != nullptr, 2000);
  EXPECT_NE(page.findChild<QWidget *>("deviceCheckWidget"), nullptr);
  // Fix round 1: arriving at the device-check screen must not by itself
  // start the real call — see RealJoinIsGatedBehindDeviceCheckConfirmation
  // for the full gate assertion; this test only re-confirms the original
  // "token arrival switches CallsPage to CallPage" behavior still holds.
  EXPECT_EQ(fakeProvider->mJoinCallCount, 0);
}

// Fix round 1: the design spec (docs/superpowers/specs/2026-09-27-native-
// call-ui-and-client-mode-design.md, §3) describes PrejoinCheck as a real
// gate — the device-check screen's own Join button is what actually starts
// the call, not something the flow blows through automatically the instant
// a token arrives. This test proves the gate holds on both sides: no real
// join() happens just because a token arrived, and clicking the
// device-check screen's own Join button is what triggers it, with the
// correct pending url/token.
//
// A FakeVideoProvider is substituted via CallsPage::setVideoProviderFactoryForTesting
// (a testing-only seam — production callers never call this and get the real
// pcm::video::LiveKitVideoProvider) rather than exercising the real LiveKit
// SDK against a fake wss:// URL: the real provider would attempt an actual
// network connection as a side effect of the state machine reaching Joining
// (see LiveKitVideoProviderSmokeTest, which tolerates up to 40s for exactly
// this "join against an unreachable URL" scenario), which would make this
// unit test slow and network-dependent for no benefit — the thing under
// test here is purely "was VideoProvider::join() invoked, and with what
// arguments," which FakeVideoProvider records directly and synchronously.
TEST(CallsPageTest, RealJoinIsGatedBehindDeviceCheckConfirmation) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, R"({
    "endpointUrl": "wss://livekit.example.test", "roomName": "room-1",
    "token": "jwt-1", "expiresAt": 999
  })");
  pcm::tokenclient::TokenBackendClient client(server.baseUrl().toString());
  pcm::video::DeviceManager deviceManager;
  CallsPage page(/*specialistMode=*/false, &deviceManager, &client);

  FakeVideoProvider *fakeProvider = nullptr;
  page.setVideoProviderFactoryForTesting([&fakeProvider]() -> pcm::video::VideoProvider * {
    fakeProvider = new FakeVideoProvider();
    return fakeProvider;
  });

  auto *codeEdit = page.findChild<QLineEdit *>("joinCodeEdit");
  auto *passcodeEdit = page.findChild<QLineEdit *>("joinPasscodeEdit");
  auto *connectButton = page.findChild<QPushButton *>("joinByCodeButton");
  ASSERT_NE(codeEdit, nullptr);
  ASSERT_NE(passcodeEdit, nullptr);
  ASSERT_NE(connectButton, nullptr);
  codeEdit->setText("code-1");
  passcodeEdit->setText("123456");
  connectButton->click();

  // Token request is asynchronous; pump the event loop until CallsPage's
  // startJoin() has actually run and constructed the (fake) provider.
  // Note: CallPage's own DeviceCheckWidget child exists from CallPage's
  // construction — findChild("deviceCheckWidget") would find it immediately,
  // before any token ever arrives — so waiting on fakeProvider directly
  // (rather than on deviceCheckWidget's mere presence, as the sibling test
  // above does) is what actually proves the token round-trip completed.
  QTRY_VERIFY_WITH_TIMEOUT(fakeProvider != nullptr, 2000);
  ASSERT_NE(page.findChild<QWidget *>("deviceCheckWidget"), nullptr);

  // The gate: a token having arrived must NOT by itself have triggered a
  // real join. Pump the event loop briefly first — VideoSession's
  // NoMeeting->Provisioned->PrejoinCheck->Joining legs are unconditional and
  // would fire within a few queued-event iterations if join() had
  // incorrectly already been called (see video_session.cpp), so giving that
  // a moment to (not) happen makes this a meaningful negative assertion
  // rather than just "it hasn't happened yet at this exact instant."
  QTest::qWait(100);
  EXPECT_EQ(fakeProvider->mJoinCallCount, 0);

  auto *joinButton = page.findChild<QPushButton *>("joinButton");
  ASSERT_NE(joinButton, nullptr);
  joinButton->click();

  // Clicking DeviceCheckWidget's own Join button is what should finally
  // trigger the real join, with the url/token that arrived earlier.
  QTRY_COMPARE_WITH_TIMEOUT(fakeProvider->mJoinCallCount, 1, 2000);
  EXPECT_EQ(fakeProvider->mLastJoinUrl, QStringLiteral("wss://livekit.example.test"));
  EXPECT_EQ(fakeProvider->mLastJoinToken, QStringLiteral("jwt-1"));
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
