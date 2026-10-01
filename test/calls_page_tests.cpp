#include "calls_page.h"
#include "token_backend_client.h"
#include "fake_token_backend_server.h"
#include "fake_video_provider.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTest>
#include <gtest/gtest.h>

using pcm::video::test::FakeVideoProvider;

// CallsPage is exercised against a real TokenBackendClient talking to a fake
// local HTTP server (same double as Task 4), rather than a hand-rolled fake
// TokenBackendClient — TokenBackendClient is a concrete, final class with no
// virtual seam, and the server-side double is already proven reliable.
//
// Asynchronous waits use ASSERT_TRUE(QTest::qWaitFor(...)), never QtTest's
// QTRY_* macros: QTRY_* report failure through QtTest's own result logger
// and then `return` from the test body — which GoogleTest does not see as a
// failure, so a timed-out QTRY_* silently turned these tests into passes
// (the release-build Qt::UniqueConnection-with-lambda bug meant no token was
// ever acted on, yet both original tests below "passed").
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
  page.findChild<QLineEdit *>("callDisplayNameEdit")->setText("Анна на встрече");
  connectButton->click();

  // Token request is asynchronous; pump the event loop until CallsPage's
  // startJoin() has actually run (proven by the fake provider having been
  // constructed), at which point CallPage is showing and its device-check
  // screen is present.
  ASSERT_TRUE(QTest::qWaitFor([&]() { return fakeProvider != nullptr; }, 2000));
  EXPECT_EQ(QJsonDocument::fromJson(server.lastBody).object().value("displayName").toString(), "Анна на встрече");
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
  ASSERT_TRUE(QTest::qWaitFor([&]() { return fakeProvider != nullptr; }, 2000));
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
  ASSERT_TRUE(QTest::qWaitFor([&]() { return fakeProvider->mJoinCallCount == 1; }, 2000));
  EXPECT_EQ(fakeProvider->mLastJoinUrl, QStringLiteral("wss://livekit.example.test"));
  EXPECT_EQ(fakeProvider->mLastJoinToken, QStringLiteral("jwt-1"));
}

// The device the user picks on the device-check screen must be the one the call
// actually uses. Previously the combo boxes only drove the preview and join() fell
// back to the default devices, so choosing a different speaker "didn't work".
TEST(CallsPageTest, DevicesChosenOnDeviceCheckAreAppliedBeforeJoining) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, R"({
    "endpointUrl": "wss://livekit.example.test", "roomName": "room-1",
    "token": "jwt-1", "expiresAt": 999
  })");
  pcm::tokenclient::TokenBackendClient client(server.baseUrl().toString());
  pcm::video::DeviceManager deviceManager;
  if (deviceManager.speakers().isEmpty() && deviceManager.microphones().isEmpty()) {
    GTEST_SKIP() << "no audio devices available to choose between";
  }
  CallsPage page(/*specialistMode=*/false, &deviceManager, &client);

  FakeVideoProvider *fakeProvider = nullptr;
  page.setVideoProviderFactoryForTesting([&fakeProvider]() -> pcm::video::VideoProvider * {
    fakeProvider = new FakeVideoProvider();
    return fakeProvider;
  });

  page.findChild<QLineEdit *>("joinCodeEdit")->setText("code-1");
  page.findChild<QLineEdit *>("joinPasscodeEdit")->setText("123456");
  page.findChild<QPushButton *>("joinByCodeButton")->click();
  ASSERT_TRUE(QTest::qWaitFor([&]() { return fakeProvider != nullptr; }, 2000));

  // Deliberately pick the LAST entry, never the first/default one.
  auto *speakerCombo = page.findChild<QComboBox *>("speakerCombo");
  auto *microphoneCombo = page.findChild<QComboBox *>("microphoneCombo");
  ASSERT_NE(speakerCombo, nullptr);
  ASSERT_NE(microphoneCombo, nullptr);
  if (speakerCombo->count() > 0) speakerCombo->setCurrentIndex(speakerCombo->count() - 1);
  if (microphoneCombo->count() > 0) microphoneCombo->setCurrentIndex(microphoneCombo->count() - 1);
  const auto chosenSpeaker = speakerCombo->currentData().toByteArray();
  const auto chosenMicrophone = microphoneCombo->currentData().toByteArray();

  page.findChild<QPushButton *>("joinButton")->click();
  ASSERT_TRUE(QTest::qWaitFor([&]() { return fakeProvider->mJoinCallCount == 1; }, 2000));

  if (!chosenSpeaker.isEmpty()) {
    EXPECT_EQ(fakeProvider->mLastSwitchedSpeaker.id(), chosenSpeaker);
  }
  if (!chosenMicrophone.isEmpty()) {
    EXPECT_EQ(fakeProvider->mLastSwitchedMicrophone.id(), chosenMicrophone);
  }
  // Applied BEFORE join(), so the provider opens the chosen devices from the start.
  EXPECT_GT(fakeProvider->mSwitchCountsAtJoin, 0);
}

TEST(CallsPageTest, BackFromDeviceCheckReturnsToTheJoinFormWithoutJoining) {
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

  page.findChild<QLineEdit *>("joinCodeEdit")->setText("code-1");
  page.findChild<QLineEdit *>("joinPasscodeEdit")->setText("123456");
  page.findChild<QPushButton *>("joinByCodeButton")->click();
  ASSERT_TRUE(QTest::qWaitFor([&]() { return fakeProvider != nullptr; }, 2000));

  auto *backButton = page.findChild<QPushButton *>("backFromDeviceCheckButton");
  ASSERT_NE(backButton, nullptr);
  EXPECT_EQ(fakeProvider->mJoinCallCount, 0);
  backButton->click();

  auto *stack = page.findChild<QStackedWidget *>(QString(), Qt::FindDirectChildrenOnly);
  ASSERT_NE(stack, nullptr);
  EXPECT_EQ(stack->currentWidget(), page.findChild<CallEntryWidget *>());
}

TEST(CallsPageTest, PastingSessioInvitationUsesItsCodeAndPasscodeForTokenRequest) {
  FakeTokenBackendServer server;
  server.setNextResponse(400, R"({"error":"invalid_invitation"})");
  pcm::tokenclient::TokenBackendClient client(server.baseUrl().toString());
  pcm::video::DeviceManager deviceManager;
  CallsPage page(/*specialistMode=*/false, &deviceManager, &client);

  page.findChild<QLineEdit *>("joinCodeEdit")
      ->setText("sessio://join?code=code-1&passcode=123456&backend=https%3A%2F%2F"
                "livekit.sessio-pcm.ru");
  page.findChild<QPushButton *>("joinByCodeButton")->click();

  ASSERT_TRUE(QTest::qWaitFor([&server]() { return !server.lastPath.isEmpty(); }, 2000));
  EXPECT_EQ(server.lastPath, QStringLiteral("/v1/invitations/code-1/client-token"));
  EXPECT_EQ(server.lastBody, QByteArrayLiteral("{\"passcode\":\"123456\"}"));
}

namespace {

const char *const kTokenResponse = R"({
    "endpointUrl": "wss://livekit.example.test", "roomName": "room-1",
    "token": "jwt-1", "expiresAt": 999
  })";

// Pumps the event loop until `session` reaches `target` — same pattern as
// VideoSessionTest::waitForState / call_page_tests.cpp's waitForState.
void waitForState(pcm::video::VideoSession &session, QSignalSpy &stateSpy,
                  pcm::video::VideoSessionState target) {
  while (session.state() != target) {
    ASSERT_TRUE(stateSpy.wait(1000)) << "timed out waiting for state " << static_cast<int>(target);
  }
}

// CallsPage's own top-level stack (entry form vs. CallPage) — the only
// QStackedWidget that is a direct child of CallsPage (CallPage has its own,
// one level further down).
QStackedWidget *topStack(CallsPage &page) {
  return page.findChild<QStackedWidget *>(QString(), Qt::FindDirectChildrenOnly);
}

void submitJoinCode(CallsPage &page, const QString &code, const QString &passcode) {
  page.findChild<QLineEdit *>("joinCodeEdit")->setText(code);
  page.findChild<QLineEdit *>("joinPasscodeEdit")->setText(passcode);
  page.findChild<QPushButton *>("joinByCodeButton")->click();
}

// Joins by code, confirms on the device-check screen, and drives the fake
// provider all the way to Connected. Returns the (CallsPage-owned) session.
pcm::video::VideoSession *joinAndConnect(CallsPage &page, FakeVideoProvider *&fakeProvider) {
  submitJoinCode(page, "code-1", "123456");
  if (!QTest::qWaitFor([&fakeProvider]() { return fakeProvider != nullptr; }, 2000)) {
    ADD_FAILURE() << "token never arrived";
    return nullptr;
  }
  // VideoSession takes Qt ownership of its provider (setParent(this)), so
  // the provider's parent IS the session CallsPage just constructed.
  auto *session = qobject_cast<pcm::video::VideoSession *>(fakeProvider->parent());
  if (session == nullptr) {
    ADD_FAILURE() << "provider is not owned by a VideoSession";
    return nullptr;
  }
  QSignalSpy stateSpy(session, &pcm::video::VideoSession::stateChanged);
  page.findChild<QPushButton *>("joinButton")->click();
  waitForState(*session, stateSpy, pcm::video::VideoSessionState::Joining);
  fakeProvider->simulateJoined();
  waitForState(*session, stateSpy, pcm::video::VideoSessionState::WaitingForParticipants);
  fakeProvider->simulateParticipantJoined({"remote"});
  waitForState(*session, stateSpy, pcm::video::VideoSessionState::Connected);
  return ::testing::Test::HasFatalFailure() ? nullptr : session;
}

} // namespace

// Fixwave group 1, bug 1: CallPage emitted leaveRequested when its Leave
// button was clicked, but nothing connected to it, so VideoSession::leave()
// was never reachable from the UI.
TEST(CallsPageTest, LeaveButtonLeavesTheActiveSession) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, kTokenResponse);
  pcm::tokenclient::TokenBackendClient client(server.baseUrl().toString());
  pcm::video::DeviceManager deviceManager;
  CallsPage page(/*specialistMode=*/false, &deviceManager, &client);
  FakeVideoProvider *fakeProvider = nullptr;
  page.setVideoProviderFactoryForTesting([&fakeProvider]() -> pcm::video::VideoProvider * {
    fakeProvider = new FakeVideoProvider();
    return fakeProvider;
  });

  auto *session = joinAndConnect(page, fakeProvider);
  ASSERT_NE(session, nullptr);
  ASSERT_EQ(fakeProvider->mLeaveCallCount, 0);
  QSignalSpy stateSpy(session, &pcm::video::VideoSession::stateChanged);

  auto *leaveButton = page.findChild<QPushButton *>("leaveButton");
  ASSERT_NE(leaveButton, nullptr);
  leaveButton->click();

  waitForState(*session, stateSpy, pcm::video::VideoSessionState::Leaving);
  EXPECT_EQ(fakeProvider->mLeaveCallCount, 1);
  fakeProvider->simulateLeft();
  waitForState(*session, stateSpy, pcm::video::VideoSessionState::Ended);
  EXPECT_EQ(topStack(page)->currentWidget(), page.findChild<CallEntryWidget *>());
}

// Fixwave group 1, bug 4: the tokenReceived connection used to be
// (re-)made inside the per-click handler with Qt::UniqueConnection, which
// Qt cannot honour for a lambda — it asserts in Debug builds and, in
// release builds, stacks one more duplicate connection per click, so the
// Nth token started N joins at once.
TEST(CallsPageTest, RepeatedJoinByCodeSubmissionsStartExactlyOneJoinPerToken) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, kTokenResponse);
  pcm::tokenclient::TokenBackendClient client(server.baseUrl().toString());
  pcm::video::DeviceManager deviceManager;
  CallsPage page(/*specialistMode=*/false, &deviceManager, &client);
  int providersConstructed = 0;
  page.setVideoProviderFactoryForTesting([&providersConstructed]() -> pcm::video::VideoProvider * {
    ++providersConstructed;
    return new FakeVideoProvider();
  });

  for (int attempt = 1; attempt <= 3; ++attempt) {
    submitJoinCode(page, "code-1", "123456");
    ASSERT_TRUE(QTest::qWaitFor([&]() { return providersConstructed == attempt; }, 2000));
    // Give any duplicate (stacked) connection time to also fire.
    QTest::qWait(100);
    ASSERT_EQ(providersConstructed, attempt) << "after join attempt " << attempt;
  }
}

TEST(CallsPageTest, RepeatedOwnMeetingJoinsStartExactlyOneJoinPerToken) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, kTokenResponse);
  pcm::tokenclient::TokenBackendClient client(server.baseUrl().toString());
  pcm::video::DeviceManager deviceManager;
  CallsPage page(/*specialistMode=*/true, &deviceManager, &client);
  page.setBearerCredentialProvider([]() { return QStringLiteral("bearer-1"); });
  page.setUpcomingMeetings({{"ref-1", "14:00", QDateTime::currentDateTime(), true, 7}});
  int providersConstructed = 0;
  page.setVideoProviderFactoryForTesting([&providersConstructed]() -> pcm::video::VideoProvider * {
    ++providersConstructed;
    return new FakeVideoProvider();
  });
  QSignalSpy eventSpy(&page, &CallsPage::eventKnownForCurrentCall);

  for (int attempt = 1; attempt <= 3; ++attempt) {
    page.findChild<QPushButton *>("joinOwnMeetingButton_ref-1")->click();
    ASSERT_TRUE(QTest::qWaitFor([&]() { return providersConstructed == attempt; }, 2000));
    QTest::qWait(100);
    ASSERT_EQ(providersConstructed, attempt) << "after join attempt " << attempt;
  }
  EXPECT_EQ(eventSpy.count(), 3);
}

// Fixwave group 1, bug 3: a failed token request (wrong passcode, expired
// invitation, network error) used to be completely silent.
TEST(CallsPageTest, TokenRequestFailureIsShownOnEntryFormAndClearedOnRetry) {
  FakeTokenBackendServer server;
  server.setNextResponse(403, R"({"error": "invalid_passcode"})");
  pcm::tokenclient::TokenBackendClient client(server.baseUrl().toString());
  pcm::video::DeviceManager deviceManager;
  CallsPage page(/*specialistMode=*/false, &deviceManager, &client);
  page.setVideoProviderFactoryForTesting([]() -> pcm::video::VideoProvider * { return new FakeVideoProvider(); });

  submitJoinCode(page, "code-1", "000000");
  auto *errorLabel = page.findChild<QLabel *>("joinErrorLabel");
  ASSERT_NE(errorLabel, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&]() { return !errorLabel->isHidden(); }, 2000));
  EXPECT_EQ(errorLabel->text(), QStringLiteral("invalid_passcode"));
  EXPECT_EQ(topStack(page)->currentWidget(), page.findChild<CallEntryWidget *>());

  // A retry must not leave the previous attempt's error lingering.
  server.setNextResponse(200, kTokenResponse);
  submitJoinCode(page, "code-1", "123456");
  EXPECT_TRUE(errorLabel->isHidden());
  EXPECT_TRUE(errorLabel->text().isEmpty());
}

// Fixwave group 1, bug 3: CallsPage switches straight back to the entry
// form when a call ends (CallPage::callEnded), so the join-failure reason
// must also be surfaced there, not only on CallPage's (immediately hidden)
// ended screen.
TEST(CallsPageTest, JoinFailureReasonIsShownOnEntryFormAfterCallEnds) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, kTokenResponse);
  pcm::tokenclient::TokenBackendClient client(server.baseUrl().toString());
  pcm::video::DeviceManager deviceManager;
  CallsPage page(/*specialistMode=*/false, &deviceManager, &client);
  FakeVideoProvider *fakeProvider = nullptr;
  page.setVideoProviderFactoryForTesting([&fakeProvider]() -> pcm::video::VideoProvider * {
    fakeProvider = new FakeVideoProvider();
    return fakeProvider;
  });

  submitJoinCode(page, "code-1", "123456");
  ASSERT_TRUE(QTest::qWaitFor([&]() { return fakeProvider != nullptr; }, 2000));
  auto *session = qobject_cast<pcm::video::VideoSession *>(fakeProvider->parent());
  ASSERT_NE(session, nullptr);
  QSignalSpy stateSpy(session, &pcm::video::VideoSession::stateChanged);
  page.findChild<QPushButton *>("joinButton")->click();
  waitForState(*session, stateSpy, pcm::video::VideoSessionState::Joining);
  fakeProvider->simulateJoinFailed("Failed to connect to the video server.");
  waitForState(*session, stateSpy, pcm::video::VideoSessionState::Failed);

  EXPECT_EQ(topStack(page)->currentWidget(), page.findChild<CallEntryWidget *>());
  auto *errorLabel = page.findChild<QLabel *>("joinErrorLabel");
  ASSERT_NE(errorLabel, nullptr);
  EXPECT_FALSE(errorLabel->isHidden());
  EXPECT_EQ(errorLabel->text(), QStringLiteral("Failed to connect to the video server."));
}

// Fixwave group 1, bug 5: an "Open Meeting" click or a forwarded sessio://
// link arriving mid-call used to switch CallsPage back to the entry form,
// hiding the live call.
TEST(CallsPageTest, PrefillAndPreselectAreIgnoredWhileACallIsActive) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, kTokenResponse);
  pcm::tokenclient::TokenBackendClient client(server.baseUrl().toString());
  pcm::video::DeviceManager deviceManager;
  CallsPage page(/*specialistMode=*/true, &deviceManager, &client);
  FakeVideoProvider *fakeProvider = nullptr;
  page.setVideoProviderFactoryForTesting([&fakeProvider]() -> pcm::video::VideoProvider * {
    fakeProvider = new FakeVideoProvider();
    return fakeProvider;
  });

  auto *session = joinAndConnect(page, fakeProvider);
  ASSERT_NE(session, nullptr);
  auto *callPage = page.findChild<CallPage *>();
  ASSERT_EQ(topStack(page)->currentWidget(), callPage);

  page.prefillJoinCode("code-2", "654321");
  EXPECT_EQ(topStack(page)->currentWidget(), callPage);
  EXPECT_EQ(page.findChild<QLineEdit *>("joinCodeEdit")->text(), QStringLiteral("code-1"));
  EXPECT_EQ(page.findChild<QLineEdit *>("joinPasscodeEdit")->text(), QStringLiteral("123456"));

  page.preselectOwnMeeting("ref-1");
  EXPECT_EQ(topStack(page)->currentWidget(), callPage);

  // Once the call has ended, both work again.
  QSignalSpy stateSpy(session, &pcm::video::VideoSession::stateChanged);
  page.findChild<QPushButton *>("leaveButton")->click();
  waitForState(*session, stateSpy, pcm::video::VideoSessionState::Leaving);
  fakeProvider->simulateLeft();
  waitForState(*session, stateSpy, pcm::video::VideoSessionState::Ended);
  page.prefillJoinCode("code-2", "654321");
  EXPECT_EQ(topStack(page)->currentWidget(), page.findChild<CallEntryWidget *>());
  EXPECT_EQ(page.findChild<QLineEdit *>("joinCodeEdit")->text(), QStringLiteral("code-2"));
}

// Fixwave group 5, bug 1: connectionLost() (a live call dropping mid-call —
// the most common real-world LiveKit failure mode) is the most visible
// connection to surface it on, since CallPage's own ended screen is hidden
// the instant callEnded() switches this page back to the entry form.
TEST(CallsPageTest, ConnectionLostReasonIsShownOnEntryFormAfterCallEnds) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, kTokenResponse);
  pcm::tokenclient::TokenBackendClient client(server.baseUrl().toString());
  pcm::video::DeviceManager deviceManager;
  CallsPage page(/*specialistMode=*/false, &deviceManager, &client);
  FakeVideoProvider *fakeProvider = nullptr;
  page.setVideoProviderFactoryForTesting([&fakeProvider]() -> pcm::video::VideoProvider * {
    fakeProvider = new FakeVideoProvider();
    return fakeProvider;
  });

  auto *session = joinAndConnect(page, fakeProvider);
  ASSERT_NE(session, nullptr);
  QSignalSpy stateSpy(session, &pcm::video::VideoSession::stateChanged);

  fakeProvider->simulateConnectionLost("room ended");
  waitForState(*session, stateSpy, pcm::video::VideoSessionState::Failed);

  EXPECT_EQ(topStack(page)->currentWidget(), page.findChild<CallEntryWidget *>());
  auto *errorLabel = page.findChild<QLabel *>("joinErrorLabel");
  ASSERT_NE(errorLabel, nullptr);
  EXPECT_FALSE(errorLabel->isHidden());
  EXPECT_EQ(errorLabel->text(), QStringLiteral("room ended"));
}

// Fixwave group 5, bug 2: startJoin() had no hasActiveCall() guard, unlike
// preselectOwnMeeting()/prefillJoinCode() — a stale/delayed token response
// (or a rapid double submission) arriving while a call is already under way
// used to destroy the in-progress VideoSession/provider out from under it.
TEST(CallsPageTest, SecondTokenArrivalWhileCallActiveIsANoOp) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, kTokenResponse);
  pcm::tokenclient::TokenBackendClient client(server.baseUrl().toString());
  pcm::video::DeviceManager deviceManager;
  CallsPage page(/*specialistMode=*/false, &deviceManager, &client);
  FakeVideoProvider *fakeProvider = nullptr;
  int providersConstructed = 0;
  page.setVideoProviderFactoryForTesting([&]() -> pcm::video::VideoProvider * {
    ++providersConstructed;
    fakeProvider = new FakeVideoProvider();
    return fakeProvider;
  });

  auto *session = joinAndConnect(page, fakeProvider);
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(providersConstructed, 1);
  auto *callPage = page.findChild<CallPage *>();
  ASSERT_EQ(topStack(page)->currentWidget(), callPage);

  // A second token arriving mid-call (stale response or rapid re-submit)
  // must not tear down the active session/provider or switch the page away.
  server.setNextResponse(200, kTokenResponse);
  submitJoinCode(page, "code-2", "654321");
  QTest::qWait(200);

  EXPECT_EQ(providersConstructed, 1) << "startJoin() must have been a no-op while a call is active";
  EXPECT_EQ(topStack(page)->currentWidget(), callPage);
  EXPECT_EQ(session->state(), pcm::video::VideoSessionState::Connected);
  EXPECT_EQ(qobject_cast<pcm::video::VideoSession *>(fakeProvider->parent()), session);
}

TEST(CallsPageTest, PrefillSwitchesToEntryFormWhenNoCallIsActive) {
  FakeTokenBackendServer server;
  pcm::tokenclient::TokenBackendClient client(server.baseUrl().toString());
  pcm::video::DeviceManager deviceManager;
  CallsPage page(/*specialistMode=*/false, &deviceManager, &client);

  page.prefillJoinCode("code-3", "111111");
  EXPECT_EQ(topStack(page)->currentWidget(), page.findChild<CallEntryWidget *>());
  EXPECT_EQ(page.findChild<QLineEdit *>("joinCodeEdit")->text(), QStringLiteral("code-3"));
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
