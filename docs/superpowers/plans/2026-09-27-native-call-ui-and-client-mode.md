# Native Call UI and Client Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a practitioner join a 1:1 LiveKit call from inside Sessio, and let a client with no therapist account join the same call by installing Sessio in "client mode" — building on top of the existing `VideoSession`/`VideoProvider` domain layer (#92) and the LiveKit token backend (#89), neither of which the desktop app currently talks to.

**Architecture:** A one-time role choice (`AppRole`: `Specialist`/`Client`) persisted in `Config` branches `Application::run()` between the existing `MainWindow` (gains a "Звонки" tab) and a new, structurally minimal `ClientModeWindow` (no `Database`/`QClientModel` dependency anywhere in its own translation unit). Both host the same new `CallsPage` widget (own-meetings list + join-by-code form, then `PrejoinCheck`/`Connected`/`Reconnecting`/`Ended` call screens driven by `VideoSession`). A new `TokenBackendClient` (Qt Network) talks to the already-implemented `/v1/meetings/{ref}/specialist-token` and `/v1/invitations/{code}/client-token` endpoints; the specialist's bearer credential is stored in the OS keychain, never in the YAML config. `sessio://` links are parsed and forwarded to a single running instance via `QLocalServer`/`QLocalSocket`, with per-OS registration in packaging.

**Tech Stack:** C++20, Qt6 (Widgets, Network, Multimedia, MultimediaWidgets, OpenGLWidgets, StateMachine), qt6keychain, GoogleTest, the existing `oatpp`-based LiveKit token backend.

## Global Constraints

- `ClientModeWindow`'s header and implementation file must never `#include` `database.h`, `qclient_model.h`, or any notes/client-record header — the privacy boundary is structural, verified by grep, not just by unused parameters.
- The specialist's token-backend bearer credential is a secret: it is written/read only through `TokenBackendCredentialStore` (OS keychain via qt6keychain), never through `pcm::config::Config` (plain YAML on disk).
- `VideoSession` records nothing on `Event`; recording `call_joined`/`call_failed` is the caller's job (existing constraint from #92, unchanged here).
- No group calls, chat, file transfer, screen share, waiting room, recording, live captions, virtual backgrounds, or reactions (roadmap §5.1/§5.5).
- `CallPage`/`CallsPage` must have zero dependency on `pcm::database::Database` or `ClientNotesPage` in their own headers — the specialist-only notes side panel is composed from the outside via a generic `setSidePanelWidget(QWidget*)` seam, so `ClientModeWindow` never transitively pulls in `Database` through the call UI either.
- Raise the application version in `CMakeLists.txt` and `src/app/application.cpp` and update `CHANGELOG.md` for this MR (AGENTS.md).
- Before committing new/changed `tr()` strings, run `cmake --build build-release --target update_translations` and translate every resulting `type="unfinished"` entry in both `translation/app_ru.ts` and `translation/app_en.ts` (AGENTS.md) — done once, in the final task, after all UI strings exist.
- New Q_OBJECT classes declared in a header with no matching `.cpp` (interfaces) must be listed explicitly as a source in any test executable that links them, exactly like `video_provider.h` already is in `test/CMakeLists.txt`, or AUTOMOC will not generate their moc and the link will fail.
- `test/CMakeLists.txt` has its own `project(PCM_Tests ...)` call, so `${PROJECT_NAME}` resolves to `PCM_Tests` there, not `Sessio` — every library a test target links must be named literally (`Sessio_config`, `Sessio_video`, `Sessio_app`, `Sessio_token_client`, `Sessio_calls_page`, ...), never `${PROJECT_NAME}_...`, inside any `test/CMakeLists.txt` snippet in this plan. `${PROJECT_NAME}_...` stays correct inside `src/*/CMakeLists.txt` and the top-level `CMakeLists.txt`, since those share the top-level `project(Sessio ...)` scope.

---

## Part A — Foundation: role, token-backend client, credentials

### Task 1: `AppRole` enum and `Config` persistence

**Files:**
- Create: `src/config/app_role.h`
- Modify: `src/config/config.h`
- Modify: `src/config/CMakeLists.txt`
- Test: `test/app_role_tests.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Produces: `enum class pcm::config::AppRole { Unset, Specialist, Client };`, `QString pcm::config::appRoleToString(AppRole)`, `std::optional<AppRole> pcm::config::appRoleFromString(const QString&)`, and `Config::app_role` (a `std::string`, default `"Unset"`).

- [ ] **Step 1: Write the failing test**

```cpp
// test/app_role_tests.cpp
#include "app_role.h"

#include <gtest/gtest.h>

using pcm::config::AppRole;
using pcm::config::appRoleFromString;
using pcm::config::appRoleToString;

TEST(AppRoleTest, RoundTripsAllValues) {
  for (const auto role : {AppRole::Unset, AppRole::Specialist, AppRole::Client}) {
    const auto text = appRoleToString(role);
    const auto parsed = appRoleFromString(text);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, role);
  }
}

TEST(AppRoleTest, UnknownStringParsesToNullopt) {
  EXPECT_FALSE(appRoleFromString(QStringLiteral("Bogus")).has_value());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

- [ ] **Step 2: Add the test target**

```cmake
# test/CMakeLists.txt — add near provider_kind_tests
add_executable(Sessio_app_role_tests app_role_tests.cpp)
target_link_libraries(Sessio_app_role_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_config
)
gtest_discover_tests(Sessio_app_role_tests)
```

- [ ] **Step 3: Run it to confirm it fails to build** (no `app_role.h` yet)

Run: `cmake --build build --target Sessio_app_role_tests`
Expected: FAIL — `app_role.h: No such file or directory`

- [ ] **Step 4: Implement `app_role.h`**

```cpp
// src/config/app_role.h
#pragma once

#include <QString>
#include <optional>

namespace pcm::config {

enum class AppRole { Unset, Specialist, Client };

[[nodiscard]] inline QString appRoleToString(const AppRole role) {
  switch (role) {
  case AppRole::Unset:
    return QStringLiteral("Unset");
  case AppRole::Specialist:
    return QStringLiteral("Specialist");
  case AppRole::Client:
    return QStringLiteral("Client");
  }
  return QStringLiteral("Unset");
}

[[nodiscard]] inline std::optional<AppRole> appRoleFromString(const QString &value) {
  if (value == QStringLiteral("Unset")) {
    return AppRole::Unset;
  }
  if (value == QStringLiteral("Specialist")) {
    return AppRole::Specialist;
  }
  if (value == QStringLiteral("Client")) {
    return AppRole::Client;
  }
  return std::nullopt;
}

} // namespace pcm::config
```

- [ ] **Step 5: Add the field to `Config` and wire the CMake source**

```cpp
// src/config/config.h — add include and field
#include "app_role.h"
// ...
struct Config {
    rfl::Skip<Poco::Path> config_pth = Poco::Path(Poco::Path::configHome())
                                           .append(kAppDirName)
                                           .append("Config.yaml");
    rfl::Flatten<DatabaseConfig> db_conf;
    std::string app_role = "Unset";

    static void save_config(const Config &conf);
    static Config read_config();
    static void migrate_legacy_directory();
};
```

```cmake
# src/config/CMakeLists.txt — add app_role.h to the qt_add_library() source list
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmake --build build --target Sessio_app_role_tests && ctest --test-dir build -R AppRoleTest --output-on-failure`
Expected: PASS (2 tests)

- [ ] **Step 7: Commit**

```bash
git add src/config/app_role.h src/config/config.h src/config/CMakeLists.txt test/app_role_tests.cpp test/CMakeLists.txt
git commit -m "feat(config): add AppRole enum and persist it on Config"
```

---

### Task 2: `RoleSelectionDialog`

**Files:**
- Create: `src/app/role_selection_dialog.h`, `src/app/role_selection_dialog.cpp`
- Modify: `src/app/CMakeLists.txt`
- Test: `test/role_selection_dialog_tests.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `pcm::config::AppRole` from Task 1.
- Produces: `class RoleSelectionDialog : public QDialog` with `[[nodiscard]] std::optional<pcm::config::AppRole> selectedRole() const;` — `std::nullopt` until the user picks one and the dialog is accepted.

- [ ] **Step 1: Write the failing test**

```cpp
// test/role_selection_dialog_tests.cpp
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
```

- [ ] **Step 2: Add the test target**

```cmake
# test/CMakeLists.txt
find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(Sessio_role_selection_dialog_tests role_selection_dialog_tests.cpp)
target_include_directories(Sessio_role_selection_dialog_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/app)
target_link_libraries(Sessio_role_selection_dialog_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Widgets
    Qt6::Test
    Sessio_app
)
set_target_properties(Sessio_role_selection_dialog_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_role_selection_dialog_tests)
```

- [ ] **Step 3: Run it to confirm it fails to build**

Run: `cmake --build build --target Sessio_role_selection_dialog_tests`
Expected: FAIL — `role_selection_dialog.h: No such file or directory`

- [ ] **Step 4: Implement the dialog**

```cpp
// src/app/role_selection_dialog.h
#pragma once

#include "app_role.h"

#include <QDialog>
#include <optional>

class RoleSelectionDialog final : public QDialog {
  Q_OBJECT

public:
  explicit RoleSelectionDialog(QWidget *parent = nullptr);

  [[nodiscard]] std::optional<pcm::config::AppRole> selectedRole() const { return mSelectedRole; }

private:
  std::optional<pcm::config::AppRole> mSelectedRole;
};
```

```cpp
// src/app/role_selection_dialog.cpp
#include "role_selection_dialog.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

RoleSelectionDialog::RoleSelectionDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Welcome to Sessio"));
  setModal(true);

  auto *layout = new QVBoxLayout(this);
  auto *prompt = new QLabel(tr("Who are you?"), this);
  layout->addWidget(prompt);

  auto *specialistButton = new QPushButton(tr("I'm a specialist"), this);
  specialistButton->setObjectName("specialistButton");
  auto *clientButton = new QPushButton(tr("I'm a client"), this);
  clientButton->setObjectName("clientButton");
  layout->addWidget(specialistButton);
  layout->addWidget(clientButton);

  connect(specialistButton, &QPushButton::clicked, this, [this]() {
    mSelectedRole = pcm::config::AppRole::Specialist;
    accept();
  });
  connect(clientButton, &QPushButton::clicked, this, [this]() {
    mSelectedRole = pcm::config::AppRole::Client;
    accept();
  });
}
```

```cmake
# src/app/CMakeLists.txt — add role_selection_dialog.cpp to qt_add_library() sources,
# and add role_selection_dialog.h next to it so AUTOMOC picks it up (it already
# has a matching .cpp, so no extra AUTOMOC wiring is needed beyond the existing
# qt_add_library() call already having AUTOMOC on for this target).
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build --target Sessio_role_selection_dialog_tests && ctest --test-dir build -R RoleSelectionDialogTest --output-on-failure`
Expected: PASS (3 tests)

- [ ] **Step 6: Commit**

```bash
git add src/app/role_selection_dialog.h src/app/role_selection_dialog.cpp src/app/CMakeLists.txt test/role_selection_dialog_tests.cpp test/CMakeLists.txt
git commit -m "feat(app): add first-launch RoleSelectionDialog"
```

---

### Task 3: Token-backend response parsing (pure functions)

**Files:**
- Create: `src/token_client/token_result.h`, `src/token_client/token_response_parser.h`, `src/token_client/token_response_parser.cpp`
- Create: `src/token_client/CMakeLists.txt`
- Modify: top-level `CMakeLists.txt` (add `add_subdirectory(src/token_client)`)
- Test: `test/token_response_parser_tests.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Produces: `struct pcm::tokenclient::TokenResult { QString endpointUrl; QString roomName; QString token; qint64 expiresAt = 0; };`, `std::optional<TokenResult> parseTokenResponse(const QByteArray &json);`, `QString parseErrorMessage(const QByteArray &json);` — matching `TokenResponseDto`/`ErrorResponseDto` field names in `token-backend/src/controller/dto.h` exactly (`endpointUrl`, `roomName`, `token`, `expiresAt`, `error`).

- [ ] **Step 1: Write the failing test**

```cpp
// test/token_response_parser_tests.cpp
#include "token_response_parser.h"

#include <gtest/gtest.h>

using pcm::tokenclient::parseErrorMessage;
using pcm::tokenclient::parseTokenResponse;

TEST(TokenResponseParserTest, ParsesWellFormedTokenResponse) {
  const QByteArray json = R"({
    "endpointUrl": "wss://livekit.example.test",
    "roomName": "room-42",
    "token": "eyJhbGciOi...",
    "expiresAt": 1234567890
  })";

  const auto result = parseTokenResponse(json);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->endpointUrl, QStringLiteral("wss://livekit.example.test"));
  EXPECT_EQ(result->roomName, QStringLiteral("room-42"));
  EXPECT_EQ(result->token, QStringLiteral("eyJhbGciOi..."));
  EXPECT_EQ(result->expiresAt, 1234567890);
}

TEST(TokenResponseParserTest, MissingFieldFailsToParse) {
  const QByteArray json = R"({"endpointUrl": "wss://x", "roomName": "r"})";
  EXPECT_FALSE(parseTokenResponse(json).has_value());
}

TEST(TokenResponseParserTest, MalformedJsonFailsToParse) {
  EXPECT_FALSE(parseTokenResponse("not json").has_value());
}

TEST(TokenResponseParserTest, ParsesErrorMessage) {
  const QByteArray json = R"({"error": "wrong_passcode"})";
  EXPECT_EQ(parseErrorMessage(json), QStringLiteral("wrong_passcode"));
}

TEST(TokenResponseParserTest, UnparsableErrorBodyFallsBackToGenericMessage) {
  EXPECT_EQ(parseErrorMessage("not json"), QStringLiteral("request_failed"));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

- [ ] **Step 2: Add the module skeleton and test target**

```cmake
# src/token_client/CMakeLists.txt
cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_token_client)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

qt_add_library(${TARGET_NAME} STATIC
        token_result.h
        token_response_parser.h
        token_response_parser.cpp
)

target_link_libraries(${TARGET_NAME} PUBLIC
        Qt6::Core
        Qt6::Network
)

target_include_directories(${TARGET_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
```

```cmake
# CMakeLists.txt (top level) — add alongside the other add_subdirectory(src/...) calls
add_subdirectory(src/token_client)
```

```cmake
# test/CMakeLists.txt
add_executable(Sessio_token_response_parser_tests token_response_parser_tests.cpp)
target_link_libraries(Sessio_token_response_parser_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_token_client
)
gtest_discover_tests(Sessio_token_response_parser_tests)
```

- [ ] **Step 3: Run it to confirm it fails to build**

Run: `cmake --build build --target Sessio_token_response_parser_tests`
Expected: FAIL — `token_response_parser.h: No such file or directory`

- [ ] **Step 4: Implement**

```cpp
// src/token_client/token_result.h
#pragma once

#include <QString>

namespace pcm::tokenclient {

struct TokenResult {
  QString endpointUrl;
  QString roomName;
  QString token;
  qint64 expiresAt = 0;
};

} // namespace pcm::tokenclient
```

```cpp
// src/token_client/token_response_parser.h
#pragma once

#include "token_result.h"

#include <QByteArray>
#include <optional>

namespace pcm::tokenclient {

[[nodiscard]] std::optional<TokenResult> parseTokenResponse(const QByteArray &json);
[[nodiscard]] QString parseErrorMessage(const QByteArray &json);

} // namespace pcm::tokenclient
```

```cpp
// src/token_client/token_response_parser.cpp
#include "token_response_parser.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace pcm::tokenclient {

std::optional<TokenResult> parseTokenResponse(const QByteArray &json) {
  const auto doc = QJsonDocument::fromJson(json);
  if (!doc.isObject()) {
    return std::nullopt;
  }
  const auto obj = doc.object();
  if (!obj.contains("endpointUrl") || !obj.contains("roomName") ||
      !obj.contains("token") || !obj.contains("expiresAt")) {
    return std::nullopt;
  }

  TokenResult result;
  result.endpointUrl = obj.value("endpointUrl").toString();
  result.roomName = obj.value("roomName").toString();
  result.token = obj.value("token").toString();
  result.expiresAt = static_cast<qint64>(obj.value("expiresAt").toDouble());
  return result;
}

QString parseErrorMessage(const QByteArray &json) {
  const auto doc = QJsonDocument::fromJson(json);
  if (!doc.isObject() || !doc.object().contains("error")) {
    return QStringLiteral("request_failed");
  }
  return doc.object().value("error").toString();
}

} // namespace pcm::tokenclient
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build --target Sessio_token_response_parser_tests && ctest --test-dir build -R TokenResponseParserTest --output-on-failure`
Expected: PASS (5 tests)

- [ ] **Step 6: Commit**

```bash
git add src/token_client CMakeLists.txt test/token_response_parser_tests.cpp test/CMakeLists.txt
git commit -m "feat(token-client): add pure JSON parsing for token-backend responses"
```

---

### Task 4: `TokenBackendClient` (HTTP layer)

**Files:**
- Create: `src/token_client/token_backend_client.h`, `src/token_client/token_backend_client.cpp`
- Create: `test/fake_token_backend_server.h`
- Modify: `src/token_client/CMakeLists.txt`
- Test: `test/token_backend_client_tests.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `pcm::tokenclient::TokenResult`, `parseTokenResponse`, `parseErrorMessage` from Task 3.
- Produces:
  ```cpp
  class TokenBackendClient : public QObject {
    Q_OBJECT
  public:
    explicit TokenBackendClient(QString baseUrl, QObject *parent = nullptr);
    void requestSpecialistToken(const QString &bearerCredential, const QString &meetingRef);
    void requestClientToken(const QString &invitationCode, const QString &passcode);
  signals:
    void tokenReceived(pcm::tokenclient::TokenResult result);
    void tokenRequestFailed(QString reason);
  };
  ```
  `requestSpecialistToken` calls `POST {baseUrl}/v1/meetings/{meetingRef}/specialist-token` with header `Authorization: Bearer {bearerCredential}` and an empty body. `requestClientToken` calls `POST {baseUrl}/v1/invitations/{invitationCode}/client-token` with no auth header and JSON body `{"passcode": "..."}`. Both endpoints and their exact contracts are already implemented server-side in `token-backend/src/controller/meetings_controller.h` and `invitations_controller.h`.

- [ ] **Step 1: Write the fake HTTP server test double**

```cpp
// test/fake_token_backend_server.h
#pragma once

#include <QByteArray>
#include <QTcpServer>
#include <QTcpSocket>

// A minimal single-connection-at-a-time HTTP/1.1 server for testing
// TokenBackendClient without a real token-backend process. Records the last
// request's method/path/headers/body and replies with a pre-configured
// status+body to the next connection.
class FakeTokenBackendServer final : public QObject {
  Q_OBJECT

public:
  explicit FakeTokenBackendServer(QObject *parent = nullptr) : QObject(parent) {
    connect(&mServer, &QTcpServer::newConnection, this, &FakeTokenBackendServer::onNewConnection);
    mServer.listen(QHostAddress::LocalHost);
  }

  [[nodiscard]] QUrl baseUrl() const {
    return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(mServer.serverPort()));
  }

  void setNextResponse(int statusCode, const QByteArray &jsonBody) {
    mStatusCode = statusCode;
    mBody = jsonBody;
  }

  QString lastPath;
  QString lastMethod;
  QString lastAuthorizationHeader;
  QByteArray lastBody;

private slots:
  void onNewConnection() {
    auto *socket = mServer.nextPendingConnection();
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
      mBuffer.append(socket->readAll());
      const auto headerEnd = mBuffer.indexOf("\r\n\r\n");
      if (headerEnd < 0) {
        return;
      }
      const auto header = QString::fromLatin1(mBuffer.left(headerEnd));
      const auto lines = header.split("\r\n");
      const auto requestLine = lines.first().split(' ');
      lastMethod = requestLine.value(0);
      lastPath = requestLine.value(1);
      for (const auto &line : lines) {
        if (line.startsWith("Authorization:", Qt::CaseInsensitive)) {
          lastAuthorizationHeader = line.section(':', 1).trimmed();
        }
      }
      const auto bodyStart = headerEnd + 4;
      if (mBuffer.size() < bodyStart) {
        return;
      }
      lastBody = mBuffer.mid(bodyStart);

      const QByteArray statusLine =
          mStatusCode == 200 ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 " + QByteArray::number(mStatusCode) + " Error\r\n";
      QByteArray response = statusLine;
      response += "Content-Type: application/json\r\n";
      response += "Content-Length: " + QByteArray::number(mBody.size()) + "\r\n";
      response += "Connection: close\r\n\r\n";
      response += mBody;
      socket->write(response);
      socket->disconnectFromHost();
      mBuffer.clear();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
  }

private:
  QTcpServer mServer;
  QByteArray mBuffer;
  int mStatusCode = 200;
  QByteArray mBody = "{}";
};
```

- [ ] **Step 2: Write the failing client test**

```cpp
// test/token_backend_client_tests.cpp
#include "token_backend_client.h"
#include "fake_token_backend_server.h"

#include <QSignalSpy>
#include <QTest>
#include <gtest/gtest.h>

using pcm::tokenclient::TokenBackendClient;
using pcm::tokenclient::TokenResult;

TEST(TokenBackendClientTest, SpecialistTokenSendsBearerHeaderAndParsesResponse) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, R"({
    "endpointUrl": "wss://livekit.example.test",
    "roomName": "room-1",
    "token": "jwt-1",
    "expiresAt": 999
  })");

  TokenBackendClient client(server.baseUrl().toString());
  QSignalSpy receivedSpy(&client, &TokenBackendClient::tokenReceived);
  QSignalSpy failedSpy(&client, &TokenBackendClient::tokenRequestFailed);

  client.requestSpecialistToken("secret-credential", "meeting-ref-1");

  ASSERT_TRUE(receivedSpy.wait(2000));
  EXPECT_EQ(failedSpy.count(), 0);
  EXPECT_EQ(server.lastMethod, QStringLiteral("POST"));
  EXPECT_EQ(server.lastPath, QStringLiteral("/v1/meetings/meeting-ref-1/specialist-token"));
  EXPECT_EQ(server.lastAuthorizationHeader, QStringLiteral("Bearer secret-credential"));

  const auto result = receivedSpy.at(0).at(0).value<TokenResult>();
  EXPECT_EQ(result.roomName, QStringLiteral("room-1"));
}

TEST(TokenBackendClientTest, ClientTokenSendsPasscodeBodyWithNoAuthHeader) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, R"({
    "endpointUrl": "wss://livekit.example.test",
    "roomName": "room-2",
    "token": "jwt-2",
    "expiresAt": 999
  })");

  TokenBackendClient client(server.baseUrl().toString());
  QSignalSpy receivedSpy(&client, &TokenBackendClient::tokenReceived);

  client.requestClientToken("code-abc", "123456");

  ASSERT_TRUE(receivedSpy.wait(2000));
  EXPECT_EQ(server.lastPath, QStringLiteral("/v1/invitations/code-abc/client-token"));
  EXPECT_TRUE(server.lastAuthorizationHeader.isEmpty());
  EXPECT_TRUE(server.lastBody.contains("123456"));
}

TEST(TokenBackendClientTest, ErrorStatusEmitsTokenRequestFailedWithServerMessage) {
  FakeTokenBackendServer server;
  server.setNextResponse(401, R"({"error": "wrong_passcode"})");

  TokenBackendClient client(server.baseUrl().toString());
  QSignalSpy failedSpy(&client, &TokenBackendClient::tokenRequestFailed);

  client.requestClientToken("code-abc", "000000");

  ASSERT_TRUE(failedSpy.wait(2000));
  EXPECT_EQ(failedSpy.at(0).at(0).toString(), QStringLiteral("wrong_passcode"));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

- [ ] **Step 3: Wire the test target and confirm it fails to build**

```cmake
# test/CMakeLists.txt
find_package(Qt6 REQUIRED COMPONENTS Network Test)
add_executable(Sessio_token_backend_client_tests token_backend_client_tests.cpp)
target_link_libraries(Sessio_token_backend_client_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Network
    Qt6::Test
    Sessio_token_client
)
set_target_properties(Sessio_token_backend_client_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_token_backend_client_tests)
```

Run: `cmake --build build --target Sessio_token_backend_client_tests`
Expected: FAIL — `token_backend_client.h: No such file or directory`

- [ ] **Step 4: Implement `TokenBackendClient`**

```cpp
// src/token_client/token_backend_client.h
#pragma once

#include "token_result.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

namespace pcm::tokenclient {

class TokenBackendClient final : public QObject {
  Q_OBJECT

public:
  explicit TokenBackendClient(QString baseUrl, QObject *parent = nullptr);

  void requestSpecialistToken(const QString &bearerCredential, const QString &meetingRef);
  void requestClientToken(const QString &invitationCode, const QString &passcode);

signals:
  void tokenReceived(pcm::tokenclient::TokenResult result);
  void tokenRequestFailed(QString reason);

private:
  void post(const QString &path, const QByteArray &body, const QString &bearerCredential);

  QNetworkAccessManager mNetworkManager;
  QString mBaseUrl;
};

} // namespace pcm::tokenclient

Q_DECLARE_METATYPE(pcm::tokenclient::TokenResult)
```

```cpp
// src/token_client/token_backend_client.cpp
#include "token_backend_client.h"
#include "token_response_parser.h"

#include <QNetworkReply>
#include <QNetworkRequest>

namespace pcm::tokenclient {

TokenBackendClient::TokenBackendClient(QString baseUrl, QObject *parent)
    : QObject(parent), mBaseUrl(std::move(baseUrl)) {
  qRegisterMetaType<TokenResult>();
}

void TokenBackendClient::requestSpecialistToken(const QString &bearerCredential,
                                                const QString &meetingRef) {
  post(QStringLiteral("/v1/meetings/%1/specialist-token").arg(meetingRef), QByteArray(),
       bearerCredential);
}

void TokenBackendClient::requestClientToken(const QString &invitationCode,
                                            const QString &passcode) {
  const QByteArray body =
      QByteArray("{\"passcode\":\"") + passcode.toUtf8() + "\"}";
  post(QStringLiteral("/v1/invitations/%1/client-token").arg(invitationCode), body, QString());
}

void TokenBackendClient::post(const QString &path, const QByteArray &body,
                              const QString &bearerCredential) {
  QNetworkRequest request(QUrl(mBaseUrl + path));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  if (!bearerCredential.isEmpty()) {
    request.setRawHeader("Authorization", "Bearer " + bearerCredential.toUtf8());
  }

  auto *reply = mNetworkManager.post(request, body);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();
    const auto responseBody = reply->readAll();
    const auto httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError || httpStatus != 200) {
      emit tokenRequestFailed(parseErrorMessage(responseBody));
      return;
    }

    const auto result = parseTokenResponse(responseBody);
    if (!result.has_value()) {
      emit tokenRequestFailed(QStringLiteral("malformed_response"));
      return;
    }
    emit tokenReceived(*result);
  });
}

} // namespace pcm::tokenclient
```

```cmake
# src/token_client/CMakeLists.txt — add token_backend_client.h/.cpp to the
# qt_add_library() source list.
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build --target Sessio_token_backend_client_tests && ctest --test-dir build -R TokenBackendClientTest --output-on-failure`
Expected: PASS (3 tests)

- [ ] **Step 6: Commit**

```bash
git add src/token_client test/fake_token_backend_server.h test/token_backend_client_tests.cpp test/CMakeLists.txt
git commit -m "feat(token-client): add TokenBackendClient HTTP layer"
```

---

### Task 5: `TokenBackendCredentialStore` (keychain-backed bearer credential)

**Files:**
- Create: `src/app/token_backend_credential_store.h`, `src/app/token_backend_credential_store.cpp`
- Create: `test/fake_token_backend_credential_store.h`
- Modify: `src/app/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  class TokenBackendCredentialStore : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual void readBearerCredential() = 0;
    virtual void writeBearerCredential(const QString &credential) = 0;
  signals:
    void readFinished(bool ok, QString credential, QString error);
    void writeFinished(bool ok, QString error);
  };
  class QtKeychainTokenBackendCredentialStore final : public TokenBackendCredentialStore { ... };
  ```
  and a `FakeTokenBackendCredentialStore` test double (in-memory, no real keychain), for Task 6 and later consumers to test against — mirroring how `CredentialStore`/`QtKeychainCredentialStore` are structured, and consistent with the fact that no existing test in this repo exercises `QtKeychainCredentialStore` against the real OS keychain either.

- [ ] **Step 1: Implement the abstract store and its keychain-backed implementation**

```cpp
// src/app/token_backend_credential_store.h
#pragma once

#include <QObject>
#include <QString>

class TokenBackendCredentialStore : public QObject {
  Q_OBJECT

public:
  using QObject::QObject;
  ~TokenBackendCredentialStore() override = default;

  virtual void readBearerCredential() = 0;
  virtual void writeBearerCredential(const QString &credential) = 0;

signals:
  void readFinished(bool ok, QString credential, QString error);
  void writeFinished(bool ok, QString error);
};

class QtKeychainTokenBackendCredentialStore final : public TokenBackendCredentialStore {
  Q_OBJECT

public:
  explicit QtKeychainTokenBackendCredentialStore(QObject *parent = nullptr);

  void readBearerCredential() override;
  void writeBearerCredential(const QString &credential) override;
};
```

```cpp
// src/app/token_backend_credential_store.cpp
#include "token_backend_credential_store.h"

#include <keychain.h>

namespace {
constexpr auto kKeychainService = "Sessio";
constexpr auto kKeychainKey = "token-backend/bearer-credential";
}

QtKeychainTokenBackendCredentialStore::QtKeychainTokenBackendCredentialStore(QObject *parent)
    : TokenBackendCredentialStore(parent) {}

void QtKeychainTokenBackendCredentialStore::readBearerCredential() {
  auto *job = new QKeychain::ReadPasswordJob(QString::fromLatin1(kKeychainService), this);
  job->setInsecureFallback(false);
  job->setKey(QString::fromLatin1(kKeychainKey));
  connect(job, &QKeychain::Job::finished, this, [this, job](QKeychain::Job *) {
    if (job->error() != QKeychain::NoError) {
      emit readFinished(false, {}, QStringLiteral("system keychain unavailable"));
    } else {
      emit readFinished(true, job->textData(), {});
    }
    job->deleteLater();
  });
  job->start();
}

void QtKeychainTokenBackendCredentialStore::writeBearerCredential(const QString &credential) {
  auto *job = new QKeychain::WritePasswordJob(QString::fromLatin1(kKeychainService), this);
  job->setInsecureFallback(false);
  job->setKey(QString::fromLatin1(kKeychainKey));
  job->setTextData(credential);
  connect(job, &QKeychain::Job::finished, this, [this, job](QKeychain::Job *) {
    emit writeFinished(job->error() == QKeychain::NoError,
                       job->error() == QKeychain::NoError
                           ? QString{}
                           : QStringLiteral("system keychain unavailable"));
    job->deleteLater();
  });
  job->start();
}
```

- [ ] **Step 2: Add the in-memory fake for consumer tests**

```cpp
// test/fake_token_backend_credential_store.h
#pragma once

#include "token_backend_credential_store.h"

// In-memory test double — never touches the real OS keychain, matching the
// same pattern used to test consumers of CredentialStore in backup_tests.cpp.
class FakeTokenBackendCredentialStore final : public TokenBackendCredentialStore {
  Q_OBJECT

public:
  using TokenBackendCredentialStore::TokenBackendCredentialStore;

  void readBearerCredential() override {
    emit readFinished(mHasCredential, mCredential, mHasCredential ? QString{} : QStringLiteral("not_set"));
  }

  void writeBearerCredential(const QString &credential) override {
    mCredential = credential;
    mHasCredential = true;
    emit writeFinished(true, {});
  }

  QString mCredential;
  bool mHasCredential = false;
};
```

- [ ] **Step 3: Wire into the build**

```cmake
# src/app/CMakeLists.txt — add token_backend_credential_store.h/.cpp to
# qt_add_library() sources, and add:
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
target_link_libraries(${TARGET_NAME} PUBLIC qt6keychain)
target_include_directories(${TARGET_NAME} PRIVATE
        ${qtkeychain_SOURCE_DIR}
        ${qtkeychain_BINARY_DIR}
)
# (qt6keychain and its include dirs are already linked into this target for
# CredentialStore — verify they are not duplicated, only add what's missing.)
```

- [ ] **Step 4: Build to confirm it compiles**

Run: `cmake --build build --target Sessio_app`
Expected: builds cleanly. There is no dedicated GoogleTest target for this task — `QtKeychainTokenBackendCredentialStore` is exercised manually (same as `QtKeychainCredentialStore`), and `FakeTokenBackendCredentialStore` is exercised through the consumer tests in Task 6 and Task 13.

- [ ] **Step 5: Commit**

```bash
git add src/app/token_backend_credential_store.h src/app/token_backend_credential_store.cpp src/app/CMakeLists.txt test/fake_token_backend_credential_store.h
git commit -m "feat(app): add keychain-backed TokenBackendCredentialStore"
```

---

### Task 6: `SettingsDialog` "LiveKit" section

**Files:**
- Modify: `src/app/settings_dialog.h`, `src/app/settings_dialog.cpp`
- Modify: `src/config/config.h` (add `token_backend_base_url` field)
- Test: `test/settings_dialog_livekit_section_tests.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `TokenBackendCredentialStore` (Task 5), `pcm::config::Config` (Task 1).
- Produces: a new section in `SettingsDialog` (following the existing `mSettingsSections`/`mSettingsStack` pattern) with a base-URL `QLineEdit` (persisted via `Config::save_config`) and a bearer-credential `QLineEdit` in password-echo mode (persisted via `TokenBackendCredentialStore::writeBearerCredential`), plus a `SettingsDialog(std::shared_ptr<Database>, TokenBackendCredentialStore*, QWidget*)` constructor overload so tests can inject `FakeTokenBackendCredentialStore`.

- [ ] **Step 1: Add the config field**

```cpp
// src/config/config.h — add alongside app_role
std::string token_backend_base_url;
```

- [ ] **Step 2: Write the failing test**

```cpp
// test/settings_dialog_livekit_section_tests.cpp
#include "settings_dialog.h"
#include "fake_token_backend_credential_store.h"

#include <QLineEdit>
#include <QPushButton>
#include <QTest>
#include <gtest/gtest.h>

TEST(SettingsDialogLiveKitSectionTest, SavingWritesBaseUrlToConfigAndCredentialToStore) {
  auto *credentialStore = new FakeTokenBackendCredentialStore();
  SettingsDialog dialog(nullptr, credentialStore);

  auto *baseUrlEdit = dialog.findChild<QLineEdit *>("liveKitBaseUrlEdit");
  auto *credentialEdit = dialog.findChild<QLineEdit *>("liveKitBearerCredentialEdit");
  auto *saveButton = dialog.findChild<QPushButton *>("liveKitSaveButton");
  ASSERT_NE(baseUrlEdit, nullptr);
  ASSERT_NE(credentialEdit, nullptr);
  ASSERT_NE(saveButton, nullptr);

  baseUrlEdit->setText("https://token.example.test");
  credentialEdit->setText("bearer-secret");
  QTest::mouseClick(saveButton, Qt::LeftButton);

  const auto savedConfig = pcm::config::Config::read_config();
  EXPECT_EQ(savedConfig.token_backend_base_url, "https://token.example.test");
  EXPECT_TRUE(credentialStore->mHasCredential);
  EXPECT_EQ(credentialStore->mCredential, QStringLiteral("bearer-secret"));
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

> Note: this test reads/writes the real `Config::config_pth` location like `test/config_tests.cpp`'s existing `ConfigTest.Initialize` does implicitly via `Config()`'s defaults — run it only inside the isolated CI/test environment, never against a developer's real profile (matches this repo's existing test hygiene for `Config`).

- [ ] **Step 3: Wire the test target and confirm it fails to build**

```cmake
# test/CMakeLists.txt
add_executable(Sessio_settings_dialog_livekit_section_tests settings_dialog_livekit_section_tests.cpp)
target_include_directories(Sessio_settings_dialog_livekit_section_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/app)
target_link_libraries(Sessio_settings_dialog_livekit_section_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Widgets
    Qt6::Test
    Sessio_app
)
set_target_properties(Sessio_settings_dialog_livekit_section_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_settings_dialog_livekit_section_tests)
```

Run: `cmake --build build --target Sessio_settings_dialog_livekit_section_tests`
Expected: FAIL — no such constructor overload / no such child widgets yet.

- [ ] **Step 4: Add the constructor overload and the LiveKit section**

```cpp
// src/app/settings_dialog.h — add
public:
  explicit SettingsDialog(std::shared_ptr<pcm::database::Database> db,
                          TokenBackendCredentialStore *credentialStore,
                          QWidget *parent = nullptr);
  // existing single-arg constructor delegates to this one with a real
  // QtKeychainTokenBackendCredentialStore owned by the dialog.
private:
  void setupLiveKitSection();
  void saveLiveKitSettings();

  TokenBackendCredentialStore *mCredentialStore{nullptr};
  QLineEdit *mLiveKitBaseUrlEdit{nullptr};
  QLineEdit *mLiveKitBearerCredentialEdit{nullptr};
```

```cpp
// src/app/settings_dialog.cpp — constructor delegation and section wiring
SettingsDialog::SettingsDialog(std::shared_ptr<pcm::database::Database> db, QWidget *parent)
    : SettingsDialog(std::move(db), new QtKeychainTokenBackendCredentialStore(), parent) {}

SettingsDialog::SettingsDialog(std::shared_ptr<pcm::database::Database> db,
                               TokenBackendCredentialStore *credentialStore, QWidget *parent)
    : QDialog(parent), mCredentialStore(credentialStore) {
  mCredentialStore->setParent(this);
  setupUi();
  setupLiveKitSection();
  loadSettings();
  connectSignals();
}

void SettingsDialog::setupLiveKitSection() {
  auto *section = new QWidget(this);
  auto *layout = new QVBoxLayout(section);

  mLiveKitBaseUrlEdit = new QLineEdit(section);
  mLiveKitBaseUrlEdit->setObjectName("liveKitBaseUrlEdit");
  mLiveKitBaseUrlEdit->setPlaceholderText(tr("https://token-backend.example.com"));
  mLiveKitBaseUrlEdit->setText(QString::fromStdString(pcm::config::Config::read_config().token_backend_base_url));

  mLiveKitBearerCredentialEdit = new QLineEdit(section);
  mLiveKitBearerCredentialEdit->setObjectName("liveKitBearerCredentialEdit");
  mLiveKitBearerCredentialEdit->setEchoMode(QLineEdit::Password);

  auto *saveButton = new QPushButton(tr("Save"), section);
  saveButton->setObjectName("liveKitSaveButton");
  connect(saveButton, &QPushButton::clicked, this, &SettingsDialog::saveLiveKitSettings);

  layout->addWidget(new QLabel(tr("Token backend URL"), section));
  layout->addWidget(mLiveKitBaseUrlEdit);
  layout->addWidget(new QLabel(tr("Bearer credential"), section));
  layout->addWidget(mLiveKitBearerCredentialEdit);
  layout->addWidget(saveButton);

  mSettingsStack->addWidget(section);
  mSettingsSections->addTab(tr("LiveKit"));
}

void SettingsDialog::saveLiveKitSettings() {
  auto conf = pcm::config::Config::read_config();
  conf.token_backend_base_url = mLiveKitBaseUrlEdit->text().toStdString();
  pcm::config::Config::save_config(conf);

  const auto credential = mLiveKitBearerCredentialEdit->text();
  if (!credential.isEmpty()) {
    mCredentialStore->writeBearerCredential(credential);
  }
}
```

> Implementer note: `setupUi()`'s exact `mSettingsSections`/`mSettingsStack` API (e.g. the real method name for adding a tab/section) must match what's already in `src/app/settings_dialog.cpp` for the existing sections — read that file's `setupUi()` before wiring this in and mirror its exact pattern rather than the illustrative names above if they differ.

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build --target Sessio_settings_dialog_livekit_section_tests && ctest --test-dir build -R SettingsDialogLiveKitSectionTest --output-on-failure`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/app/settings_dialog.h src/app/settings_dialog.cpp src/config/config.h test/settings_dialog_livekit_section_tests.cpp test/CMakeLists.txt
git commit -m "feat(app): add LiveKit settings section for token-backend URL and credential"
```

---

## Part B — Make LiveKit meetings creatable

### Task 7: Functional `LiveKitMeetingProvider`

**Files:**
- Modify: `src/meeting/meeting_provider.h` (extend `MeetingCreateRequest`)
- Modify: `src/meeting/livekit_meeting_provider.h`, `src/meeting/livekit_meeting_provider.cpp`
- Modify: `src/meeting/CMakeLists.txt`
- Test: `test/livekit_meeting_provider_tests.cpp` (extend existing file)

**Interfaces:**
- Consumes: `TokenBackendClient` is HTTP-only (specialist/client tokens); meeting create/invalidate need their own two endpoints (`POST /v1/meetings`, `POST /v1/meetings/{ref}/invalidate`) which `TokenBackendClient` does not yet expose. Add them there rather than duplicating HTTP logic:
  ```cpp
  // added to TokenBackendClient (src/token_client/token_backend_client.h/.cpp)
  void requestCreateMeeting(const QString &bearerCredential, const QString &scheduledStartIso,
                            const QString &scheduledEndIso);
  void requestInvalidateMeeting(const QString &bearerCredential, const QString &meetingRef);
  signals:
    void meetingCreated(pcm::tokenclient::MeetingCreateResult result);
    void meetingCreateFailed(QString reason);
    void meetingInvalidated();
    void meetingInvalidateFailed(QString reason);
  ```
  with `struct MeetingCreateResult { QString meetingRef; QString invitationUrl; QString passcode; };` (from `CreateMeetingResponseDto` in `token-backend/src/controller/dto.h`) and a matching `parseMeetingCreateResponse(const QByteArray&)` pure function in `token_response_parser.h/.cpp`, tested the same way as Task 3.
- Produces: `LiveKitMeetingProvider::create()` calls `requestCreateMeeting` with the request's scheduled window and the credential read once at startup (see Task 15) and cached on the provider; on `meetingCreated`, emits `MeetingProvider::created(descriptor)` with `descriptor.kind = ProviderKind::LiveKit`, `descriptor.meetingRef = result.meetingRef`, `descriptor.invitationState = result.invitationUrl + "|" + result.passcode` (mirrors `meeting_descriptor.h`'s existing single-string `invitationState` field — the exact packing format is this provider's own concern, since nothing else parses it yet). `cancel()` calls `requestInvalidateMeeting`.

- [ ] **Step 1: Extend `MeetingCreateRequest`**

```cpp
// src/meeting/meeting_provider.h
struct MeetingCreateRequest {
  QString rawMeetingUrl;
  QString scheduledStartIso; // ISO-8601, only used by the LiveKit provider
  QString scheduledEndIso;   // ISO-8601, only used by the LiveKit provider
};
```

- [ ] **Step 2: Add `MeetingCreateResult` parsing to `token_client`, with its own failing test first**

```cpp
// test/token_response_parser_tests.cpp — add
TEST(TokenResponseParserTest, ParsesMeetingCreateResponse) {
  const QByteArray json = R"({
    "meetingRef": "ref-1",
    "invitationUrl": "https://invite.example.test/code-1",
    "passcode": "123456",
    "scheduledStart": "2026-10-01T10:00:00Z",
    "scheduledEnd": "2026-10-01T10:50:00Z"
  })";
  const auto result = pcm::tokenclient::parseMeetingCreateResponse(json);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->meetingRef, QStringLiteral("ref-1"));
  EXPECT_EQ(result->invitationUrl, QStringLiteral("https://invite.example.test/code-1"));
  EXPECT_EQ(result->passcode, QStringLiteral("123456"));
}
```

Run: `cmake --build build --target Sessio_token_response_parser_tests`
Expected: FAIL — `parseMeetingCreateResponse` undeclared.

```cpp
// src/token_client/token_result.h — add
struct MeetingCreateResult {
  QString meetingRef;
  QString invitationUrl;
  QString passcode;
};
```

```cpp
// src/token_client/token_response_parser.h — add
[[nodiscard]] std::optional<MeetingCreateResult> parseMeetingCreateResponse(const QByteArray &json);
```

```cpp
// src/token_client/token_response_parser.cpp — add
std::optional<MeetingCreateResult> parseMeetingCreateResponse(const QByteArray &json) {
  const auto doc = QJsonDocument::fromJson(json);
  if (!doc.isObject()) {
    return std::nullopt;
  }
  const auto obj = doc.object();
  if (!obj.contains("meetingRef") || !obj.contains("invitationUrl") || !obj.contains("passcode")) {
    return std::nullopt;
  }
  MeetingCreateResult result;
  result.meetingRef = obj.value("meetingRef").toString();
  result.invitationUrl = obj.value("invitationUrl").toString();
  result.passcode = obj.value("passcode").toString();
  return result;
}
```

Run: `cmake --build build --target Sessio_token_response_parser_tests && ctest --test-dir build -R TokenResponseParserTest --output-on-failure`
Expected: PASS

- [ ] **Step 3: Add `requestCreateMeeting`/`requestInvalidateMeeting` to `TokenBackendClient`, with a failing test first**

```cpp
// test/token_backend_client_tests.cpp — add
TEST(TokenBackendClientTest, CreateMeetingSendsScheduleAndBearerHeader) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, R"({
    "meetingRef": "ref-1", "invitationUrl": "https://x/code-1", "passcode": "111111",
    "scheduledStart": "2026-10-01T10:00:00Z", "scheduledEnd": "2026-10-01T10:50:00Z"
  })");

  pcm::tokenclient::TokenBackendClient client(server.baseUrl().toString());
  QSignalSpy createdSpy(&client, &pcm::tokenclient::TokenBackendClient::meetingCreated);

  client.requestCreateMeeting("secret", "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");

  ASSERT_TRUE(createdSpy.wait(2000));
  EXPECT_EQ(server.lastPath, QStringLiteral("/v1/meetings"));
  EXPECT_EQ(server.lastAuthorizationHeader, QStringLiteral("Bearer secret"));
  EXPECT_TRUE(server.lastBody.contains("2026-10-01T10:00:00Z"));
}
```

Add to `TokenBackendClient` (mirrors `requestSpecialistToken`/`requestClientToken`'s `post()` helper exactly):

```cpp
// token_backend_client.h — add signals and methods
void requestCreateMeeting(const QString &bearerCredential, const QString &scheduledStartIso,
                          const QString &scheduledEndIso);
void requestInvalidateMeeting(const QString &bearerCredential, const QString &meetingRef);
signals:
  void meetingCreated(pcm::tokenclient::MeetingCreateResult result);
  void meetingCreateFailed(QString reason);
  void meetingInvalidated();
  void meetingInvalidateFailed(QString reason);
```

```cpp
// token_backend_client.cpp — add, following post()'s existing lambda-per-call
// style but parsing into MeetingCreateResult instead of TokenResult, and (for
// invalidate) treating any 2xx as success with no body to parse.
```

- [ ] **Step 4: Run the client test to verify it passes**

Run: `ctest --test-dir build -R TokenBackendClientTest --output-on-failure`
Expected: PASS

- [ ] **Step 5: Wire `LiveKitMeetingProvider` to the client, with a failing test first**

```cpp
// test/livekit_meeting_provider_tests.cpp — replace the existing
// "always fails" tests (they described the intentional stub; that
// description is now false) with:
#include "livekit_meeting_provider.h"
#include "fake_token_backend_server.h"

#include <QSignalSpy>
#include <gtest/gtest.h>

TEST(LiveKitMeetingProviderTest, CreateCallsTokenBackendAndEmitsCreatedDescriptor) {
  FakeTokenBackendServer server;
  server.setNextResponse(200, R"({
    "meetingRef": "ref-9", "invitationUrl": "https://x/code-9", "passcode": "222222",
    "scheduledStart": "2026-10-01T10:00:00Z", "scheduledEnd": "2026-10-01T10:50:00Z"
  })");

  pcm::meeting::LiveKitMeetingProvider provider(server.baseUrl().toString(), "bearer-secret");
  QSignalSpy createdSpy(&provider, &pcm::meeting::MeetingProvider::created);

  provider.create({.scheduledStartIso = "2026-10-01T10:00:00Z",
                   .scheduledEndIso = "2026-10-01T10:50:00Z"});

  ASSERT_TRUE(createdSpy.wait(2000));
  const auto descriptor = createdSpy.at(0).at(0).value<pcm::meeting::MeetingDescriptor>();
  EXPECT_EQ(descriptor.kind, pcm::meeting::ProviderKind::LiveKit);
  EXPECT_EQ(descriptor.meetingRef, QStringLiteral("ref-9"));
}
```

Run it: FAIL — no such constructor.

```cpp
// src/meeting/livekit_meeting_provider.h
#pragma once

#include "meeting_provider.h"
#include "token_backend_client.h"

#include <memory>

namespace pcm::meeting {

class LiveKitMeetingProvider final : public MeetingProvider {
  Q_OBJECT

public:
  LiveKitMeetingProvider(QString tokenBackendBaseUrl, QString bearerCredential,
                         QObject *parent = nullptr);

  void create(const MeetingCreateRequest &request) override;
  void cancel(const QString &meetingRef) override;

private:
  std::unique_ptr<pcm::tokenclient::TokenBackendClient> mClient;
  QString mBearerCredential;
};

} // namespace pcm::meeting
```

```cpp
// src/meeting/livekit_meeting_provider.cpp
#include "livekit_meeting_provider.h"

namespace pcm::meeting {

LiveKitMeetingProvider::LiveKitMeetingProvider(QString tokenBackendBaseUrl,
                                               QString bearerCredential, QObject *parent)
    : MeetingProvider(parent),
      mClient(std::make_unique<pcm::tokenclient::TokenBackendClient>(std::move(tokenBackendBaseUrl))),
      mBearerCredential(std::move(bearerCredential)) {
  connect(mClient.get(), &pcm::tokenclient::TokenBackendClient::meetingCreated, this,
          [this](const pcm::tokenclient::MeetingCreateResult &result) {
            MeetingDescriptor descriptor;
            descriptor.kind = ProviderKind::LiveKit;
            descriptor.meetingRef = result.meetingRef;
            descriptor.invitationState = result.invitationUrl + "|" + result.passcode;
            emit created(descriptor);
          });
  connect(mClient.get(), &pcm::tokenclient::TokenBackendClient::meetingCreateFailed, this,
          &MeetingProvider::createFailed);
  connect(mClient.get(), &pcm::tokenclient::TokenBackendClient::meetingInvalidated, this,
          &MeetingProvider::canceled);
  connect(mClient.get(), &pcm::tokenclient::TokenBackendClient::meetingInvalidateFailed, this,
          &MeetingProvider::cancelFailed);
}

void LiveKitMeetingProvider::create(const MeetingCreateRequest &request) {
  mClient->requestCreateMeeting(mBearerCredential, request.scheduledStartIso, request.scheduledEndIso);
}

void LiveKitMeetingProvider::cancel(const QString &meetingRef) {
  mClient->requestInvalidateMeeting(mBearerCredential, meetingRef);
}

} // namespace pcm::meeting
```

```cmake
# src/meeting/CMakeLists.txt
find_package(Qt6 REQUIRED COMPONENTS Core)
target_link_libraries(${TARGET_NAME} PUBLIC
        Qt6::Core
        ${PROJECT_NAME}_token_client
)
```

```cmake
# top-level CMakeLists.txt — src/meeting now depends on src/token_client;
# add_subdirectory(src/token_client) must appear before add_subdirectory(src/meeting).
```

> Implementer note: `MeetingCoordinator` currently constructs `LiveKitMeetingProvider` with no arguments (`using MeetingProvider::MeetingProvider`). It now needs the base URL and bearer credential at construction time. Thread them through `MeetingCoordinator`'s own constructor (`MeetingCoordinator(QString tokenBackendBaseUrl, QString bearerCredential, QObject *parent = nullptr)`), update `meeting_coordinator_tests.cpp`'s existing `MeetingCoordinator` construction calls to pass empty strings (those tests exercise `ExternalUrl` and the failure path, not real LiveKit calls), and update the one production call site in `src/app/application.cpp` once Task 17 has the credential available.

- [ ] **Step 6: Run the provider test to verify it passes**

Run: `ctest --test-dir build -R LiveKitMeetingProviderTest --output-on-failure`
Expected: PASS. Also re-run `Sessio_meeting_coordinator_tests` to confirm the constructor-signature change didn't break it.

- [ ] **Step 7: Commit**

```bash
git add src/meeting src/token_client test/livekit_meeting_provider_tests.cpp test/token_backend_client_tests.cpp test/token_response_parser_tests.cpp test/meeting_coordinator_tests.cpp CMakeLists.txt
git commit -m "feat(meeting): make LiveKitMeetingProvider create/cancel real meetings"
```

---

### Task 8: `LiveKit` provider-kind selector in `QEventDetailsWidget`

**Files:**
- Modify: `src/pages/event_info_page/qevent_details_widget.h`, `.cpp`

**Interfaces:**
- Consumes: `pcm::meeting::ProviderKind` (existing), `MeetingCreateRequest::scheduledStartIso/scheduledEndIso` (Task 7).
- Produces: a provider-kind chooser shown when "Online session" is on; `updateMeetingViaCoordinator()` branches on it instead of hardcoding `ExternalUrl`.

- [ ] **Step 1: Add the chooser widget next to the online-session switch**

```cpp
// qevent_details_widget.h — add
oclero::qlementine::SegmentedControl *mProviderKindControl = nullptr;
```

```cpp
// qevent_details_widget.cpp — in initUi(), right after mOnlineSessionSwitch is built
mProviderKindControl = new oclero::qlementine::SegmentedControl(this);
mProviderKindControl->addTab(tr("External link"));
mProviderKindControl->addTab(tr("LiveKit"));
mProviderKindControl->setCurrentIndex(0);
// ... add mProviderKindControl to the same layout row as mOnlineSessionSwitch,
// following this file's existing pattern for mRepeatTypeControl (another
// SegmentedControl already built the same way a few lines above).
```

- [ ] **Step 2: Show/hide it with the online-session switch**

```cpp
// onOnlineSessionToggled(bool checked) — add
mProviderKindControl->setVisible(checked);
```

- [ ] **Step 3: Branch `updateMeetingViaCoordinator()` on the selected provider kind**

```cpp
void QEventDetailsWidget::updateMeetingViaCoordinator() {
  if (!mCurrentEvent) {
    return;
  }

  const bool wasOnline = mCurrentEvent->providerKind().has_value();
  const bool isOnline = mOnlineSessionSwitch->isChecked();

  if (!isOnline) {
    if (wasOnline && mMeetingCoordinator) {
      mMeetingCoordinator->cancelMeeting(*mCurrentEvent->providerKind(), mCurrentEvent->meetingRef());
    }
    applyProviderFields(std::nullopt, QString{}, std::nullopt, QString{});
    return;
  }

  if (!mMeetingCoordinator) {
    const auto trimmedUrl = mMeetingUrlEdit->text().trimmed();
    applyProviderFields(pcm::meeting::ProviderKind::ExternalUrl, trimmedUrl,
                       mCurrentEvent->invitationState(), trimmedUrl);
    return;
  }

  const bool wantsLiveKit = mProviderKindControl->currentIndex() == 1;
  if (wantsLiveKit) {
    const auto startIso = QDateTime(mUI->mEventDate->date(), mUI->mTimeFrom->time(),
                                    QTimeZone::systemTimeZone())
                              .toUTC()
                              .toString(Qt::ISODate);
    const auto endIso = QDateTime(mUI->mEventDate->date(), mUI->mTimeTo->time(),
                                  QTimeZone::systemTimeZone())
                            .toUTC()
                            .toString(Qt::ISODate);
    mMeetingCoordinator->createMeeting(
        pcm::meeting::ProviderKind::LiveKit,
        {.scheduledStartIso = startIso, .scheduledEndIso = endIso});
    return;
  }

  mMeetingCoordinator->createMeeting(pcm::meeting::ProviderKind::ExternalUrl,
                                     {.rawMeetingUrl = mMeetingUrlEdit->text()});
}
```

> Implementer note: confirm the exact field names on `Ui::EventDetails` for the end-time widget (`mTimeTo` is used elsewhere in this file per `onTimeToChanged`) before wiring `endIso` above.

- [ ] **Step 4: Manual verification** (this widget has no existing dedicated GoogleTest target — `QEventInfoPage`/`QEventDetailsWidget` are exercised through the running app; note this in the task-reviewer package as "⚠️ Cannot verify from diff — no automated test covers this widget" per SDD's own process, and verify manually per Step 5 below)

- [ ] **Step 5: Manual smoke check**

Run: `cmake --build build-release --parallel && ` then launch via `scripts/run-dev-isolated.sh` (per this project's GUI-testing-isolation convention — never launch a raw dev build against real app data), open an event, toggle "Online session", switch to "LiveKit", save, and confirm the event now carries a `meetingRef` from a real `POST /v1/meetings` call against a locally running token-backend instance (`token-backend/`'s own README describes running it locally).

- [ ] **Step 6: Commit**

```bash
git add src/pages/event_info_page/qevent_details_widget.h src/pages/event_info_page/qevent_details_widget.cpp
git commit -m "feat(event-info): allow choosing LiveKit as the online-session provider"
```

---

## Part C — Call UI building blocks

### Task 9: `ClientModeWindow` shell

**Files:**
- Create: `src/app/client_mode_window.h`, `src/app/client_mode_window.cpp`
- Modify: `src/app/CMakeLists.txt`
- Test: `test/client_mode_window_structural_test.sh` (grep-based, not GoogleTest)

**Interfaces:**
- Produces: `class ClientModeWindow final : public QMainWindow` with no constructor arguments beyond `QWidget *parent = nullptr`, and (for now, until Task 15) an empty body.

- [ ] **Step 1: Implement the empty shell**

```cpp
// src/app/client_mode_window.h
#pragma once

#include <QMainWindow>

class ClientModeWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit ClientModeWindow(QWidget *parent = nullptr);
};
```

```cpp
// src/app/client_mode_window.cpp
#include "client_mode_window.h"

ClientModeWindow::ClientModeWindow(QWidget *parent) : QMainWindow(parent) {
  setWindowTitle(tr("Sessio"));
}
```

- [ ] **Step 2: Write the structural boundary check**

```bash
# test/client_mode_window_structural_test.sh
#!/usr/bin/env bash
# ClientModeWindow must never gain a dependency on Database/QClientModel — the
# spec's privacy boundary is structural. This grep-based check runs in CI
# (wired into test/CMakeLists.txt as a CTest case in Step 3) rather than as a
# GoogleTest, since what it verifies is an absence of an #include, not runtime
# behavior.
set -euo pipefail
FILE="$1"
if grep -qE '#include\s*"(database|qclient_model)\.h"' "$FILE"; then
  echo "FORBIDDEN: $FILE must not include database.h or qclient_model.h" >&2
  exit 1
fi
echo "OK: $FILE has no forbidden includes"
```

- [ ] **Step 3: Wire it into CTest**

```cmake
# test/CMakeLists.txt
add_test(NAME ClientModeWindowHeaderHasNoDatabaseDependency
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/client_mode_window_structural_test.sh
                 ${CMAKE_SOURCE_DIR}/src/app/client_mode_window.h)
add_test(NAME ClientModeWindowSourceHasNoDatabaseDependency
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/client_mode_window_structural_test.sh
                 ${CMAKE_SOURCE_DIR}/src/app/client_mode_window.cpp)
```

- [ ] **Step 4: Run it to verify it passes**

Run: `chmod +x test/client_mode_window_structural_test.sh && cmake --build build && ctest --test-dir build -R ClientModeWindow --output-on-failure`
Expected: PASS (2 tests)

- [ ] **Step 5: Commit**

```bash
git add src/app/client_mode_window.h src/app/client_mode_window.cpp src/app/CMakeLists.txt test/client_mode_window_structural_test.sh test/CMakeLists.txt
git commit -m "feat(app): add ClientModeWindow shell with a structural no-Database check"
```

---

### Task 10: `CallEntryWidget`

**Files:**
- Create: `src/pages/calls_page/call_entry_widget.h`, `src/pages/calls_page/call_entry_widget.cpp`
- Create: `src/pages/calls_page/CMakeLists.txt`
- Modify: top-level `CMakeLists.txt`
- Test: `test/call_entry_widget_tests.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  struct UpcomingMeeting { QString meetingRef; QString title; QDateTime startTime; bool joinEnabled; int64_t eventId; };

  class CallEntryWidget final : public QWidget {
    Q_OBJECT
  public:
    explicit CallEntryWidget(bool showOwnMeetings, QWidget *parent = nullptr);
    void setUpcomingMeetings(const QList<UpcomingMeeting> &meetings);
    void preselectOwnMeeting(const QString &meetingRef);
    void prefillJoinCode(const QString &code, const QString &passcode);
  signals:
    void ownMeetingJoinRequested(QString meetingRef);
    void joinByCodeRequested(QString code, QString passcode);
  };
  ```
  When `showOwnMeetings` is `false` (client mode), the own-meetings list is never constructed/shown at all — only the code+passcode form exists.

- [ ] **Step 1: Write the failing test**

```cpp
// test/call_entry_widget_tests.cpp
#include "call_entry_widget.h"

#include <QApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <gtest/gtest.h>

TEST(CallEntryWidgetTest, SpecialistModeShowsOwnMeetingsListAndEmitsJoinRequest) {
  CallEntryWidget widget(/*showOwnMeetings=*/true);
  widget.setUpcomingMeetings({{"ref-1", "14:00 — Иванова", QDateTime::currentDateTime(), true, 7}});

  auto *joinButton = widget.findChild<QPushButton *>("joinOwnMeetingButton_ref-1");
  ASSERT_NE(joinButton, nullptr);

  QSignalSpy joinSpy(&widget, &CallEntryWidget::ownMeetingJoinRequested);
  QTest::mouseClick(joinButton, Qt::LeftButton);

  ASSERT_EQ(joinSpy.count(), 1);
  EXPECT_EQ(joinSpy.at(0).at(0).toString(), QStringLiteral("ref-1"));
}

TEST(CallEntryWidgetTest, ClientModeHasNoOwnMeetingsList) {
  CallEntryWidget widget(/*showOwnMeetings=*/false);
  auto *list = widget.findChild<QWidget *>("ownMeetingsList");
  EXPECT_EQ(list, nullptr);
}

TEST(CallEntryWidgetTest, SubmittingCodeFormEmitsJoinByCodeRequested) {
  CallEntryWidget widget(/*showOwnMeetings=*/false);
  auto *codeEdit = widget.findChild<QLineEdit *>("joinCodeEdit");
  auto *passcodeEdit = widget.findChild<QLineEdit *>("joinPasscodeEdit");
  auto *connectButton = widget.findChild<QPushButton *>("joinByCodeButton");
  ASSERT_NE(codeEdit, nullptr);
  ASSERT_NE(passcodeEdit, nullptr);
  ASSERT_NE(connectButton, nullptr);

  codeEdit->setText("code-1");
  passcodeEdit->setText("123456");
  QSignalSpy joinSpy(&widget, &CallEntryWidget::joinByCodeRequested);
  QTest::mouseClick(connectButton, Qt::LeftButton);

  ASSERT_EQ(joinSpy.count(), 1);
  EXPECT_EQ(joinSpy.at(0).at(0).toString(), QStringLiteral("code-1"));
  EXPECT_EQ(joinSpy.at(0).at(1).toString(), QStringLiteral("123456"));
}

TEST(CallEntryWidgetTest, PrefillJoinCodePopulatesForm) {
  CallEntryWidget widget(false);
  widget.prefillJoinCode("code-2", "654321");

  EXPECT_EQ(widget.findChild<QLineEdit *>("joinCodeEdit")->text(), QStringLiteral("code-2"));
  EXPECT_EQ(widget.findChild<QLineEdit *>("joinPasscodeEdit")->text(), QStringLiteral("654321"));
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

- [ ] **Step 2: Add the module skeleton and test target**

```cmake
# src/pages/calls_page/CMakeLists.txt
cmake_minimum_required(VERSION 3.28)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_calls_page)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

qt_add_library(${TARGET_NAME} STATIC
        call_entry_widget.h
        call_entry_widget.cpp
)

target_link_libraries(${TARGET_NAME} PUBLIC
        Qt6::Core
        Qt6::Widgets
)
target_include_directories(${TARGET_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
```

```cmake
# top-level CMakeLists.txt
add_subdirectory(src/pages/calls_page)
```

```cmake
# test/CMakeLists.txt
add_executable(Sessio_call_entry_widget_tests call_entry_widget_tests.cpp)
target_link_libraries(Sessio_call_entry_widget_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Widgets
    Qt6::Test
    Sessio_calls_page
)
set_target_properties(Sessio_call_entry_widget_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_call_entry_widget_tests)
```

Run: `cmake --build build --target Sessio_call_entry_widget_tests`
Expected: FAIL — `call_entry_widget.h: No such file or directory`

- [ ] **Step 3: Implement `CallEntryWidget`**

```cpp
// src/pages/calls_page/call_entry_widget.h
#pragma once

#include <QDateTime>
#include <QList>
#include <QWidget>

class QLineEdit;
class QVBoxLayout;

struct UpcomingMeeting {
  QString meetingRef;
  QString title;
  QDateTime startTime;
  bool joinEnabled = false;
  int64_t eventId = 0;
};

class CallEntryWidget final : public QWidget {
  Q_OBJECT

public:
  explicit CallEntryWidget(bool showOwnMeetings, QWidget *parent = nullptr);

  void setUpcomingMeetings(const QList<UpcomingMeeting> &meetings);
  void preselectOwnMeeting(const QString &meetingRef);
  void prefillJoinCode(const QString &code, const QString &passcode);

signals:
  void ownMeetingJoinRequested(QString meetingRef);
  void joinByCodeRequested(QString code, QString passcode);

private:
  bool mShowOwnMeetings;
  QWidget *mOwnMeetingsList{nullptr};
  QVBoxLayout *mOwnMeetingsLayout{nullptr};
  QLineEdit *mCodeEdit{nullptr};
  QLineEdit *mPasscodeEdit{nullptr};
};
```

```cpp
// src/pages/calls_page/call_entry_widget.cpp
#include "call_entry_widget.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

CallEntryWidget::CallEntryWidget(const bool showOwnMeetings, QWidget *parent)
    : QWidget(parent), mShowOwnMeetings(showOwnMeetings) {
  auto *layout = new QVBoxLayout(this);

  if (mShowOwnMeetings) {
    layout->addWidget(new QLabel(tr("Your meetings today"), this));
    mOwnMeetingsList = new QWidget(this);
    mOwnMeetingsList->setObjectName("ownMeetingsList");
    mOwnMeetingsLayout = new QVBoxLayout(mOwnMeetingsList);
    layout->addWidget(mOwnMeetingsList);
  }

  layout->addWidget(new QLabel(tr("Join another meeting"), this));
  mCodeEdit = new QLineEdit(this);
  mCodeEdit->setObjectName("joinCodeEdit");
  mCodeEdit->setPlaceholderText(tr("Invitation code or link"));
  mPasscodeEdit = new QLineEdit(this);
  mPasscodeEdit->setObjectName("joinPasscodeEdit");
  mPasscodeEdit->setPlaceholderText(tr("Passcode (6 digits)"));
  auto *connectButton = new QPushButton(tr("Connect"), this);
  connectButton->setObjectName("joinByCodeButton");

  layout->addWidget(mCodeEdit);
  layout->addWidget(mPasscodeEdit);
  layout->addWidget(connectButton);
  layout->addStretch();

  connect(connectButton, &QPushButton::clicked, this, [this]() {
    emit joinByCodeRequested(mCodeEdit->text(), mPasscodeEdit->text());
  });
}

void CallEntryWidget::setUpcomingMeetings(const QList<UpcomingMeeting> &meetings) {
  if (!mShowOwnMeetings) {
    return;
  }
  QLayoutItem *item;
  while ((item = mOwnMeetingsLayout->takeAt(0)) != nullptr) {
    delete item->widget();
    delete item;
  }
  for (const auto &meeting : meetings) {
    auto *row = new QWidget(mOwnMeetingsList);
    auto *rowLayout = new QVBoxLayout(row);
    rowLayout->addWidget(new QLabel(meeting.title, row));
    auto *joinButton = new QPushButton(tr("Join"), row);
    joinButton->setObjectName("joinOwnMeetingButton_" + meeting.meetingRef);
    joinButton->setEnabled(meeting.joinEnabled);
    connect(joinButton, &QPushButton::clicked, this,
            [this, ref = meeting.meetingRef]() { emit ownMeetingJoinRequested(ref); });
    rowLayout->addWidget(joinButton);
    mOwnMeetingsLayout->addWidget(row);
  }
}

void CallEntryWidget::preselectOwnMeeting(const QString &meetingRef) {
  if (!mShowOwnMeetings) {
    return;
  }
  if (auto *button = findChild<QPushButton *>("joinOwnMeetingButton_" + meetingRef)) {
    button->setFocus();
  }
}

void CallEntryWidget::prefillJoinCode(const QString &code, const QString &passcode) {
  mCodeEdit->setText(code);
  mPasscodeEdit->setText(passcode);
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target Sessio_call_entry_widget_tests && ctest --test-dir build -R CallEntryWidgetTest --output-on-failure`
Expected: PASS (4 tests)

- [ ] **Step 5: Commit**

```bash
git add src/pages/calls_page CMakeLists.txt test/call_entry_widget_tests.cpp test/CMakeLists.txt
git commit -m "feat(calls-page): add CallEntryWidget with own-meetings list and join-by-code form"
```

---

### Task 11: `DeviceCheckWidget` (PrejoinCheck screen)

**Files:**
- Create: `src/pages/calls_page/device_check_widget.h`, `src/pages/calls_page/device_check_widget.cpp`
- Modify: `src/pages/calls_page/CMakeLists.txt`
- Test: `test/device_check_widget_tests.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `pcm::video::DeviceManager` (existing, `src/video/device_manager.h`), `pcm::video::VideoCaptureAdapter` (existing, `src/video/video_capture_adapter.h` — already exposes `previewSink()`/`start(QCameraDevice)`/`stop()`, reused here purely for the local preview, independent of the capture `LiveKitVideoProvider::join()` starts later for the real call).
- Produces:
  ```cpp
  class DeviceCheckWidget final : public QWidget {
    Q_OBJECT
  public:
    explicit DeviceCheckWidget(pcm::video::DeviceManager *deviceManager, QWidget *parent = nullptr);
    ~DeviceCheckWidget() override;
  signals:
    void joinRequested();
  protected:
    void showEvent(QShowEvent *event) override; // starts the preview
    void hideEvent(QHideEvent *event) override; // stops the preview
  };
  ```

- [ ] **Step 1: Write the failing test**

```cpp
// test/device_check_widget_tests.cpp
#include "device_check_widget.h"
#include "device_manager.h"

#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <gtest/gtest.h>

TEST(DeviceCheckWidgetTest, HasCameraMicrophoneSpeakerSelectors) {
  pcm::video::DeviceManager deviceManager;
  DeviceCheckWidget widget(&deviceManager);

  EXPECT_NE(widget.findChild<QComboBox *>("cameraCombo"), nullptr);
  EXPECT_NE(widget.findChild<QComboBox *>("microphoneCombo"), nullptr);
  EXPECT_NE(widget.findChild<QComboBox *>("speakerCombo"), nullptr);
}

TEST(DeviceCheckWidgetTest, ClickingJoinEmitsJoinRequested) {
  pcm::video::DeviceManager deviceManager;
  DeviceCheckWidget widget(&deviceManager);
  auto *joinButton = widget.findChild<QPushButton *>("joinButton");
  ASSERT_NE(joinButton, nullptr);

  QSignalSpy joinSpy(&widget, &DeviceCheckWidget::joinRequested);
  QTest::mouseClick(joinButton, Qt::LeftButton);

  EXPECT_EQ(joinSpy.count(), 1);
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

- [ ] **Step 2: Wire the test target and confirm it fails to build**

```cmake
# test/CMakeLists.txt
add_executable(Sessio_device_check_widget_tests device_check_widget_tests.cpp)
target_link_libraries(Sessio_device_check_widget_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Widgets
    Qt6::Test
    Sessio_calls_page
    Sessio_video
)
set_target_properties(Sessio_device_check_widget_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_device_check_widget_tests)
```

Run: `cmake --build build --target Sessio_device_check_widget_tests`
Expected: FAIL — `device_check_widget.h: No such file or directory`

- [ ] **Step 3: Implement `DeviceCheckWidget`**

```cpp
// src/pages/calls_page/device_check_widget.h
#pragma once

#include "device_manager.h"
#include "video_capture_adapter.h"

#include <QLabel>
#include <QWidget>
#include <memory>

class QComboBox;
class QPushButton;

class DeviceCheckWidget final : public QWidget {
  Q_OBJECT

public:
  explicit DeviceCheckWidget(pcm::video::DeviceManager *deviceManager, QWidget *parent = nullptr);
  ~DeviceCheckWidget() override;

signals:
  void joinRequested();

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  void restartPreview();

  pcm::video::DeviceManager *mDeviceManager;
  std::unique_ptr<pcm::video::VideoCaptureAdapter> mPreviewAdapter;
  QComboBox *mCameraCombo{nullptr};
  QComboBox *mMicrophoneCombo{nullptr};
  QComboBox *mSpeakerCombo{nullptr};
  QLabel *mPreviewLabel{nullptr};
};
```

```cpp
// src/pages/calls_page/device_check_widget.cpp
#include "device_check_widget.h"

#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVideoFrame>

DeviceCheckWidget::DeviceCheckWidget(pcm::video::DeviceManager *deviceManager, QWidget *parent)
    : QWidget(parent), mDeviceManager(deviceManager),
      mPreviewAdapter(std::make_unique<pcm::video::VideoCaptureAdapter>()) {
  auto *layout = new QVBoxLayout(this);

  mPreviewLabel = new QLabel(this);
  mPreviewLabel->setMinimumSize(320, 180);
  layout->addWidget(mPreviewLabel);

  mCameraCombo = new QComboBox(this);
  mCameraCombo->setObjectName("cameraCombo");
  for (const auto &camera : mDeviceManager->cameras()) {
    mCameraCombo->addItem(camera.description());
  }
  mMicrophoneCombo = new QComboBox(this);
  mMicrophoneCombo->setObjectName("microphoneCombo");
  for (const auto &mic : mDeviceManager->microphones()) {
    mMicrophoneCombo->addItem(mic.description());
  }
  mSpeakerCombo = new QComboBox(this);
  mSpeakerCombo->setObjectName("speakerCombo");
  for (const auto &speaker : mDeviceManager->speakers()) {
    mSpeakerCombo->addItem(speaker.description());
  }
  layout->addWidget(mCameraCombo);
  layout->addWidget(mMicrophoneCombo);
  layout->addWidget(mSpeakerCombo);

  auto *joinButton = new QPushButton(tr("Join"), this);
  joinButton->setObjectName("joinButton");
  connect(joinButton, &QPushButton::clicked, this, &DeviceCheckWidget::joinRequested);
  layout->addWidget(joinButton);

  connect(mPreviewAdapter->previewSink(), &QVideoSink::videoFrameChanged, this,
          [this](const QVideoFrame &frame) {
            if (frame.isValid()) {
              mPreviewLabel->setPixmap(QPixmap::fromImage(frame.toImage())
                                           .scaled(mPreviewLabel->size(), Qt::KeepAspectRatio));
            }
          });
  connect(mCameraCombo, &QComboBox::currentIndexChanged, this, &DeviceCheckWidget::restartPreview);
}

DeviceCheckWidget::~DeviceCheckWidget() { mPreviewAdapter->stop(); }

void DeviceCheckWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  restartPreview();
}

void DeviceCheckWidget::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  mPreviewAdapter->stop();
}

void DeviceCheckWidget::restartPreview() {
  const auto cameras = mDeviceManager->cameras();
  const auto index = mCameraCombo->currentIndex();
  if (index < 0 || index >= cameras.size()) {
    return;
  }
  mPreviewAdapter->start(cameras.at(index));
}
```

- [ ] **Step 4: Add `device_check_widget.h`/`.cpp` to the module's CMakeLists and link `Sessio_video`**

```cmake
# src/pages/calls_page/CMakeLists.txt — add device_check_widget.h/.cpp to sources, and:
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Multimedia MultimediaWidgets)
target_link_libraries(${TARGET_NAME} PUBLIC
        Qt6::Core
        Qt6::Widgets
        Qt6::Multimedia
        Qt6::MultimediaWidgets
        ${PROJECT_NAME}_video
)
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build --target Sessio_device_check_widget_tests && ctest --test-dir build -R DeviceCheckWidgetTest --output-on-failure`
Expected: PASS (2 tests)

- [ ] **Step 6: Commit**

```bash
git add src/pages/calls_page test/device_check_widget_tests.cpp test/CMakeLists.txt
git commit -m "feat(calls-page): add DeviceCheckWidget with camera/mic/speaker selection and live preview"
```

---

### Task 12: `CallPage` (VideoSession-driven states)

**Files:**
- Create: `src/pages/calls_page/call_page.h`, `src/pages/calls_page/call_page.cpp`
- Modify: `src/pages/calls_page/CMakeLists.txt`
- Test: `test/call_page_tests.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `pcm::video::VideoSession`/`VideoSessionState` (existing, #92), `pcm::video::FakeVideoProvider` (existing test double, `test/fake_video_provider.h`), `pcm::video::RemoteVideoRenderer` (existing), `DeviceCheckWidget` (Task 11).
- Produces:
  ```cpp
  class CallPage final : public QWidget {
    Q_OBJECT
  public:
    explicit CallPage(pcm::video::DeviceManager *deviceManager, QWidget *parent = nullptr);

    void attachSession(pcm::video::VideoSession *session); // does not take ownership
    void setSidePanelWidget(QWidget *panel); // nullptr clears it; hidden until toggled
    void setSidePanelToggleVisible(bool visible); // false in client mode: no toggle at all
  signals:
    void leaveRequested();
    void callEnded();
  public slots:
    void onSessionStateChanged(pcm::video::VideoSessionState state);
  };
  ```
  `CallPage`'s header must not `#include` `database.h` or `client_notes_page.h` — the side panel is any generic `QWidget*`, composed from outside (Global Constraints).

- [ ] **Step 1: Write the failing test**

```cpp
// test/call_page_tests.cpp
#include "call_page.h"
#include "fake_video_provider.h"
#include "device_manager.h"

#include <QApplication>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <gtest/gtest.h>

using pcm::video::FakeVideoProvider;
using pcm::video::VideoSession;
using pcm::video::VideoSessionState;

TEST(CallPageTest, StartsOnDeviceCheckScreen) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  EXPECT_NE(page.findChild<QWidget *>("deviceCheckWidget"), nullptr);
  EXPECT_EQ(page.findChild<QWidget *>("connectedView"), nullptr);
}

TEST(CallPageTest, ConnectedStateShowsConnectedViewWithLeaveButton) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);

  session.join("wss://x", "token");
  provider->simulateJoined();
  provider->simulateRemoteParticipantConnected();
  QCoreApplication::processEvents();

  EXPECT_NE(page.findChild<QWidget *>("connectedView"), nullptr);
  auto *leaveButton = page.findChild<QPushButton *>("leaveButton");
  ASSERT_NE(leaveButton, nullptr);

  QSignalSpy leaveSpy(&page, &CallPage::leaveRequested);
  leaveButton->click();
  EXPECT_EQ(leaveSpy.count(), 1);
}

TEST(CallPageTest, EndedStateEmitsCallEnded) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  auto *provider = new FakeVideoProvider();
  VideoSession session(provider);
  page.attachSession(&session);
  QSignalSpy endedSpy(&page, &CallPage::callEnded);

  session.join("wss://x", "token");
  provider->simulateJoined();
  session.leave();
  provider->simulateLeft();
  QCoreApplication::processEvents();

  EXPECT_EQ(endedSpy.count(), 1);
}

TEST(CallPageTest, SidePanelToggleHiddenByDefaultUntilMadeVisible) {
  pcm::video::DeviceManager deviceManager;
  CallPage page(&deviceManager);
  EXPECT_EQ(page.findChild<QPushButton *>("notesToggleButton"), nullptr);

  page.setSidePanelToggleVisible(true);
  EXPECT_NE(page.findChild<QPushButton *>("notesToggleButton"), nullptr);
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

- [ ] **Step 2: Wire the test target and confirm it fails to build**

```cmake
# test/CMakeLists.txt
find_package(Qt6 REQUIRED COMPONENTS StateMachine Test)
add_executable(Sessio_call_page_tests
    ${CMAKE_SOURCE_DIR}/src/video/video_provider.h
    call_page_tests.cpp
)
target_include_directories(Sessio_call_page_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_call_page_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Widgets
    Qt6::Test
    Qt6::StateMachine
    Sessio_calls_page
    Sessio_video
)
set_target_properties(Sessio_call_page_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_call_page_tests)
```

Run: `cmake --build build --target Sessio_call_page_tests`
Expected: FAIL — `call_page.h: No such file or directory`

- [ ] **Step 3: Implement `CallPage`**

```cpp
// src/pages/calls_page/call_page.h
#pragma once

#include "device_check_widget.h"
#include "device_manager.h"
#include "remote_video_renderer.h"
#include "video_session.h"
#include "video_session_state.h"

#include <QPointer>
#include <QWidget>

class QPushButton;
class QStackedWidget;

class CallPage final : public QWidget {
  Q_OBJECT

public:
  explicit CallPage(pcm::video::DeviceManager *deviceManager, QWidget *parent = nullptr);

  void attachSession(pcm::video::VideoSession *session);
  void setSidePanelWidget(QWidget *panel);
  void setSidePanelToggleVisible(bool visible);

signals:
  void leaveRequested();
  void callEnded();

public slots:
  void onSessionStateChanged(pcm::video::VideoSessionState state);

private:
  void buildDeviceCheckScreen(pcm::video::DeviceManager *deviceManager);
  void buildConnectedScreen();

  QStackedWidget *mStack{nullptr};
  DeviceCheckWidget *mDeviceCheck{nullptr};
  QWidget *mConnectingScreen{nullptr};
  QWidget *mConnectedView{nullptr};
  QWidget *mReconnectingBanner{nullptr};
  QWidget *mEndedScreen{nullptr};
  QWidget *mSidePanelHost{nullptr};
  QPushButton *mNotesToggleButton{nullptr};
  QPointer<QWidget> mSidePanel;
  QPointer<pcm::video::VideoSession> mSession;
};
```

```cpp
// src/pages/calls_page/call_page.cpp
#include "call_page.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

CallPage::CallPage(pcm::video::DeviceManager *deviceManager, QWidget *parent) : QWidget(parent) {
  auto *outer = new QVBoxLayout(this);
  mStack = new QStackedWidget(this);
  outer->addWidget(mStack);

  buildDeviceCheckScreen(deviceManager);

  mConnectingScreen = new QWidget(this);
  new QVBoxLayout(mConnectingScreen);
  static_cast<QVBoxLayout *>(mConnectingScreen->layout())
      ->addWidget(new QLabel(tr("Connecting..."), mConnectingScreen));
  mStack->addWidget(mConnectingScreen);

  buildConnectedScreen();

  mReconnectingBanner = new QLabel(tr("Reconnecting..."), this);
  mReconnectingBanner->setObjectName("reconnectingBanner");
  mReconnectingBanner->setVisible(false);
  outer->addWidget(mReconnectingBanner);

  mEndedScreen = new QWidget(this);
  new QVBoxLayout(mEndedScreen);
  static_cast<QVBoxLayout *>(mEndedScreen->layout())
      ->addWidget(new QLabel(tr("Call ended."), mEndedScreen));
  mStack->addWidget(mEndedScreen);

  mStack->setCurrentWidget(mDeviceCheck);
}

void CallPage::buildDeviceCheckScreen(pcm::video::DeviceManager *deviceManager) {
  mDeviceCheck = new DeviceCheckWidget(deviceManager, this);
  mDeviceCheck->setObjectName("deviceCheckWidget");
  connect(mDeviceCheck, &DeviceCheckWidget::joinRequested, this, [this]() {
    if (mSession) {
      mSession->join(QString(), QString()); // url/token are supplied by the
                                            // caller before attachSession();
                                            // see CallsPage (Task 13) for the
                                            // real join(url, token) call.
    }
  });
  mStack->addWidget(mDeviceCheck);
}

void CallPage::buildConnectedScreen() {
  mConnectedView = new QWidget(this);
  mConnectedView->setObjectName("connectedView");
  auto *layout = new QVBoxLayout(mConnectedView);

  auto *videoRow = new QHBoxLayout();
  videoRow->addWidget(new pcm::video::RemoteVideoRenderer(mConnectedView), 1);
  mSidePanelHost = new QWidget(mConnectedView);
  mSidePanelHost->setVisible(false);
  new QVBoxLayout(mSidePanelHost);
  videoRow->addWidget(mSidePanelHost);
  layout->addLayout(videoRow);

  auto *controls = new QHBoxLayout();
  auto *leaveButton = new QPushButton(tr("Leave"), mConnectedView);
  leaveButton->setObjectName("leaveButton");
  connect(leaveButton, &QPushButton::clicked, this, &CallPage::leaveRequested);
  controls->addWidget(leaveButton);
  layout->addLayout(controls);

  mStack->addWidget(mConnectedView);
}

void CallPage::attachSession(pcm::video::VideoSession *session) {
  mSession = session;
  connect(session, &pcm::video::VideoSession::stateChanged, this, &CallPage::onSessionStateChanged);
}

void CallPage::setSidePanelWidget(QWidget *panel) {
  if (mSidePanel) {
    mSidePanelHost->layout()->removeWidget(mSidePanel);
    mSidePanel->setParent(nullptr);
  }
  mSidePanel = panel;
  if (panel) {
    mSidePanelHost->layout()->addWidget(panel);
  }
}

void CallPage::setSidePanelToggleVisible(bool visible) {
  if (visible && !mNotesToggleButton) {
    mNotesToggleButton = new QPushButton(tr("Notes"), mConnectedView);
    mNotesToggleButton->setObjectName("notesToggleButton");
    mNotesToggleButton->setCheckable(true);
    connect(mNotesToggleButton, &QPushButton::toggled, this,
            [this](bool checked) { mSidePanelHost->setVisible(checked); });
    static_cast<QHBoxLayout *>(mConnectedView->layout()->itemAt(1)->layout())
        ->addWidget(mNotesToggleButton);
  } else if (!visible && mNotesToggleButton) {
    delete mNotesToggleButton;
    mNotesToggleButton = nullptr;
  }
}

void CallPage::onSessionStateChanged(const pcm::video::VideoSessionState state) {
  using pcm::video::VideoSessionState;
  mReconnectingBanner->setVisible(state == VideoSessionState::Reconnecting);

  switch (state) {
  case VideoSessionState::NoMeeting:
  case VideoSessionState::Provisioned:
  case VideoSessionState::PrejoinCheck:
    mStack->setCurrentWidget(mDeviceCheck);
    break;
  case VideoSessionState::Joining:
  case VideoSessionState::WaitingForClient:
    mStack->setCurrentWidget(mConnectingScreen);
    break;
  case VideoSessionState::Connected:
  case VideoSessionState::Reconnecting:
    mStack->setCurrentWidget(mConnectedView);
    break;
  case VideoSessionState::Leaving:
    break;
  case VideoSessionState::Ended:
  case VideoSessionState::Failed:
    mStack->setCurrentWidget(mEndedScreen);
    emit callEnded();
    break;
  }
}
```

> Implementer note: `buildDeviceCheckScreen()`'s `joinRequested` handler above calls `mSession->join(QString(), QString())` as a placeholder for wiring order — Task 13 (`CallsPage`) replaces this with the real flow (request a token, then call `join(url, token)` with the real values), since `CallPage` itself has no token client and must not gain one — it only knows about `VideoSession`. Leave a `// TODO(Task 13): real join wiring happens in CallsPage` comment here so this is not mistaken for the finished behavior; `CallsPage`'s own test (Task 13) is what proves the real flow works end-to-end.

- [ ] **Step 4: Add `call_page.h`/`.cpp` to the module CMakeLists and link `Sessio_video`**

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build --target Sessio_call_page_tests && ctest --test-dir build -R CallPageTest --output-on-failure`
Expected: PASS (4 tests)

- [ ] **Step 6: Commit**

```bash
git add src/pages/calls_page test/call_page_tests.cpp test/CMakeLists.txt
git commit -m "feat(calls-page): add VideoSession-driven CallPage with device-check, connected and ended screens"
```

---

### Task 13: `CallsPage` (join orchestration)

**Files:**
- Create: `src/pages/calls_page/calls_page.h`, `src/pages/calls_page/calls_page.cpp`
- Modify: `src/pages/calls_page/CMakeLists.txt`
- Test: `test/calls_page_tests.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `CallEntryWidget` (Task 10), `CallPage` (Task 12), `pcm::tokenclient::TokenBackendClient` (Task 4), `pcm::video::LiveKitVideoProvider` (existing).
- Produces:
  ```cpp
  class CallsPage final : public QWidget {
    Q_OBJECT
  public:
    CallsPage(bool specialistMode, pcm::video::DeviceManager *deviceManager,
              pcm::tokenclient::TokenBackendClient *tokenClient, QWidget *parent = nullptr);

    void setBearerCredentialProvider(std::function<QString()> provider); // specialist mode only
    void setUpcomingMeetings(const QList<UpcomingMeeting> &meetings);
    void preselectOwnMeeting(const QString &meetingRef);
    void prefillJoinCode(const QString &code, const QString &passcode);
    void setSidePanelWidget(QWidget *panel);
  };
  ```
  This is the widget both `MainWindow` (Task 14) and `ClientModeWindow` (Task 15) embed directly; it owns the `CallEntryWidget`⟷`CallPage` `QStackedWidget` switch and the join sequence (own-meeting or code path → `TokenBackendClient` → construct `LiveKitVideoProvider`+`VideoSession` → `attachSession` → `session->join(url, token)`).

- [ ] **Step 1: Write the failing test** (using a fake token client double, since `LiveKitVideoProvider` needs a real LiveKit SDK connection that this unit test must not attempt)

```cpp
// test/calls_page_tests.cpp
#include "calls_page.h"
#include "token_backend_client.h"
#include "fake_token_backend_server.h"

#include <QApplication>
#include <QPushButton>
#include <QLineEdit>
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
```

- [ ] **Step 2: Wire the test target and confirm it fails to build**

```cmake
# test/CMakeLists.txt
find_package(Qt6 REQUIRED COMPONENTS StateMachine Network Test)
add_executable(Sessio_calls_page_tests
    ${CMAKE_SOURCE_DIR}/src/video/video_provider.h
    calls_page_tests.cpp
)
target_include_directories(Sessio_calls_page_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_calls_page_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Widgets
    Qt6::StateMachine
    Qt6::Network
    Qt6::Test
    Sessio_calls_page
    Sessio_token_client
    Sessio_video
)
set_target_properties(Sessio_calls_page_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_calls_page_tests)
```

Run: `cmake --build build --target Sessio_calls_page_tests`
Expected: FAIL — `calls_page.h: No such file or directory`

- [ ] **Step 3: Implement `CallsPage`**

```cpp
// src/pages/calls_page/calls_page.h
#pragma once

#include "call_entry_widget.h"
#include "call_page.h"
#include "device_manager.h"
#include "token_backend_client.h"

#include <QWidget>
#include <functional>
#include <memory>

class QStackedWidget;

class CallsPage final : public QWidget {
  Q_OBJECT

public:
  CallsPage(bool specialistMode, pcm::video::DeviceManager *deviceManager,
            pcm::tokenclient::TokenBackendClient *tokenClient, QWidget *parent = nullptr);

  void setBearerCredentialProvider(std::function<QString()> provider);
  void setUpcomingMeetings(const QList<UpcomingMeeting> &meetings);
  void preselectOwnMeeting(const QString &meetingRef);
  void prefillJoinCode(const QString &code, const QString &passcode);
  void setSidePanelWidget(QWidget *panel);

signals:
  void eventKnownForCurrentCall(int64_t eventId);

private:
  void startJoin(const QString &url, const QString &token);

  pcm::video::DeviceManager *mDeviceManager;
  pcm::tokenclient::TokenBackendClient *mTokenClient;
  std::function<QString()> mBearerCredentialProvider;
  QStackedWidget *mStack{nullptr};
  CallEntryWidget *mEntryWidget{nullptr};
  CallPage *mCallPage{nullptr};
  std::unique_ptr<pcm::video::VideoSession> mSession;
  QList<UpcomingMeeting> mUpcomingMeetings;
  std::optional<int64_t> mCurrentEventId;
};
```

```cpp
// src/pages/calls_page/calls_page.cpp
#include "calls_page.h"
#include "livekit_video_provider.h"

#include <QStackedWidget>
#include <QVBoxLayout>

CallsPage::CallsPage(const bool specialistMode, pcm::video::DeviceManager *deviceManager,
                     pcm::tokenclient::TokenBackendClient *tokenClient, QWidget *parent)
    : QWidget(parent), mDeviceManager(deviceManager), mTokenClient(tokenClient) {
  auto *layout = new QVBoxLayout(this);
  mStack = new QStackedWidget(this);
  layout->addWidget(mStack);

  mEntryWidget = new CallEntryWidget(specialistMode, this);
  mCallPage = new CallPage(mDeviceManager, this);
  mCallPage->setSidePanelToggleVisible(specialistMode);
  mStack->addWidget(mEntryWidget);
  mStack->addWidget(mCallPage);
  mStack->setCurrentWidget(mEntryWidget);

  connect(mEntryWidget, &CallEntryWidget::joinByCodeRequested, this,
          [this](const QString &code, const QString &passcode) {
            mCurrentEventId.reset(); // a code/passcode join never has a known Event
            connect(mTokenClient, &pcm::tokenclient::TokenBackendClient::tokenReceived, this,
                    [this](const pcm::tokenclient::TokenResult &result) {
                      startJoin(result.endpointUrl, result.token);
                    },
                    Qt::UniqueConnection);
            mTokenClient->requestClientToken(code, passcode);
          });

  connect(mEntryWidget, &CallEntryWidget::ownMeetingJoinRequested, this,
          [this](const QString &meetingRef) {
            if (!mBearerCredentialProvider) {
              return;
            }
            mCurrentEventId.reset();
            for (const auto &meeting : mUpcomingMeetings) {
              if (meeting.meetingRef == meetingRef) {
                mCurrentEventId = meeting.eventId;
                break;
              }
            }
            connect(mTokenClient, &pcm::tokenclient::TokenBackendClient::tokenReceived, this,
                    [this](const pcm::tokenclient::TokenResult &result) {
                      startJoin(result.endpointUrl, result.token);
                    },
                    Qt::UniqueConnection);
            mTokenClient->requestSpecialistToken(mBearerCredentialProvider(), meetingRef);
          });

  connect(mCallPage, &CallPage::callEnded, this, [this]() { mStack->setCurrentWidget(mEntryWidget); });
}

void CallsPage::setBearerCredentialProvider(std::function<QString()> provider) {
  mBearerCredentialProvider = std::move(provider);
}

void CallsPage::setUpcomingMeetings(const QList<UpcomingMeeting> &meetings) {
  mUpcomingMeetings = meetings;
  mEntryWidget->setUpcomingMeetings(meetings);
}

void CallsPage::preselectOwnMeeting(const QString &meetingRef) {
  mStack->setCurrentWidget(mEntryWidget);
  mEntryWidget->preselectOwnMeeting(meetingRef);
}

void CallsPage::prefillJoinCode(const QString &code, const QString &passcode) {
  mStack->setCurrentWidget(mEntryWidget);
  mEntryWidget->prefillJoinCode(code, passcode);
}

void CallsPage::setSidePanelWidget(QWidget *panel) { mCallPage->setSidePanelWidget(panel); }

void CallsPage::startJoin(const QString &url, const QString &token) {
  auto *provider = new pcm::video::LiveKitVideoProvider();
  mSession = std::make_unique<pcm::video::VideoSession>(provider);
  mCallPage->attachSession(mSession.get());
  mStack->setCurrentWidget(mCallPage);
  mSession->join(url, token);
  if (mCurrentEventId.has_value()) {
    emit eventKnownForCurrentCall(*mCurrentEventId);
  }
}
```

> Implementer note: `CallPage::buildDeviceCheckScreen()`'s `joinRequested` handler (Task 12) called `mSession->join(QString(), QString())`, which is now dead/wrong once `CallsPage::startJoin()` already calls the real `join(url, token)` right after `attachSession()`. Remove that placeholder call from `CallPage` in this task (it was flagged with a `// TODO(Task 13)` comment specifically for this) — `DeviceCheckWidget::joinRequested` firing while already `Provisioned`/past `NoMeeting` is now a no-op path that Task 12's `CallPage` should simply drop, since `startJoin()` is the only real join trigger going forward.

- [ ] **Step 4: Add `calls_page.h`/`.cpp` to the module CMakeLists**

```cmake
# src/pages/calls_page/CMakeLists.txt — add calls_page.h/.cpp, and link:
target_link_libraries(${TARGET_NAME} PUBLIC
        ${PROJECT_NAME}_token_client
)
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build --target Sessio_calls_page_tests && ctest --test-dir build -R CallsPageTest --output-on-failure`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/pages/calls_page test/calls_page_tests.cpp test/CMakeLists.txt
git commit -m "feat(calls-page): add CallsPage assembling entry form, join orchestration, and call screens"
```

---

## Part D — Integration

### Task 14: `MainWindow` "Звонки" tab

**Files:**
- Modify: `src/app/main_window.h`, `src/app/main_window.cpp`
- Modify: `src/app/CMakeLists.txt` (link `${PROJECT_NAME}_calls_page`, `${PROJECT_NAME}_token_client`)
- Test: `test/main_window_calls_tab_tests.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `CallsPage` (Task 13).
- Produces: `MainWindow::Pages::calls`, `MainWindow::addCallsPage(pcm::video::DeviceManager*, pcm::tokenclient::TokenBackendClient*, std::function<QString()> bearerCredentialProvider)`, a new `mBtnCalls` `TabButton`, wired in `connectSignals()` exactly like `mBtnNotes`/`Pages::clientNotes`.

- [ ] **Step 1: Write the failing test**

```cpp
// test/main_window_calls_tab_tests.cpp
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
```

- [ ] **Step 2: Wire the test target and confirm it fails to build**

```cmake
# test/CMakeLists.txt
add_executable(Sessio_main_window_calls_tab_tests main_window_calls_tab_tests.cpp)
target_link_libraries(Sessio_main_window_calls_tab_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Widgets
    Sessio_app
    Sessio_token_client
)
gtest_discover_tests(Sessio_main_window_calls_tab_tests)
```

Run: `cmake --build build --target Sessio_main_window_calls_tab_tests`
Expected: FAIL — `Pages::calls`/`addCallsPage` undeclared.

- [ ] **Step 3: Add the page and tab**

```cpp
// src/app/main_window.h
#include "calls_page.h"
#include "token_backend_client.h"
// ...
enum class Pages { clientInfo, eventInfo, analytics, clientCard, clientNotes, calls };
// ...
void addCallsPage(pcm::video::DeviceManager *deviceManager,
                  pcm::tokenclient::TokenBackendClient *tokenClient,
                  std::function<QString()> bearerCredentialProvider);
// ...
TabButton *mBtnCalls{nullptr};
```

```cpp
// src/app/main_window.cpp — constructor, alongside the other TabButtons
mBtnCalls = new TabButton(QIcon(":/icons/video-solid-full.svg"), tr("Calls"), this);
mUi->verticalLayout->addWidget(mBtnCalls);

// new method, mirroring addClientNotesPage
void MainWindow::addCallsPage(pcm::video::DeviceManager *deviceManager,
                              pcm::tokenclient::TokenBackendClient *tokenClient,
                              std::function<QString()> bearerCredentialProvider) {
  auto *page = new CallsPage(/*specialistMode=*/true, deviceManager, tokenClient, this);
  page->setBearerCredentialProvider(std::move(bearerCredentialProvider));
  mPages.insertOrAssign(Pages::calls, page);

  const int index = mUi->stackedWidget->addWidget(page);
  mPagesIndex.insertOrAssign(Pages::calls, index);
}

// connectSignals() — add
connect(mBtnCalls, &QPushButton::clicked, [this]() { showPage(Pages::calls, mBtnCalls); });
```

> Implementer note: `:/icons/video-solid-full.svg` must exist in `resources/`; if it doesn't, check `resources/icons/` for the closest existing Font Awesome "solid" icon already in the repo (the existing tabs all use this same icon family) and use that instead, or add the missing SVG asset to `resources/themes.qrc` following the pattern of the other icon entries there.

- [ ] **Step 4: Wire notes-panel composition for the specialist Calls tab**

This is done at the call site that owns both `Database` and the `CallsPage` — i.e. wherever `addCallsPage()` is called from (Task 17's `Application::run()`), not inside `MainWindow` itself: once a `CallsPage::preselectOwnMeeting`/join flow resolves a client for the current event, construct a `ClientNotesPage(db)` and call `mainWindow->getPage(MainWindow::Pages::calls)`, `dynamic_cast<CallsPage*>(...)`, `->setSidePanelWidget(notesPage)`. Document this explicitly in Task 17 rather than duplicating it here.

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build --target Sessio_main_window_calls_tab_tests && ctest --test-dir build -R MainWindowCallsTabTest --output-on-failure`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/app/main_window.h src/app/main_window.cpp src/app/CMakeLists.txt test/main_window_calls_tab_tests.cpp test/CMakeLists.txt
git commit -m "feat(app): add \"Звонки\" (Calls) tab to MainWindow"
```

---

### Task 15: `ClientModeWindow` integration

**Files:**
- Modify: `src/app/client_mode_window.h`, `src/app/client_mode_window.cpp`
- Modify: `src/app/CMakeLists.txt`
- Test: `test/client_mode_window_tests.cpp`, and re-run `test/client_mode_window_structural_test.sh` (Task 9) unchanged

**Interfaces:**
- Consumes: `CallsPage` (Task 13), `pcm::video::DeviceManager`, `pcm::tokenclient::TokenBackendClient`.
- Produces: `ClientModeWindow(pcm::video::DeviceManager*, pcm::tokenclient::TokenBackendClient*, QWidget *parent = nullptr)` hosting a `CallsPage(/*specialistMode=*/false, ...)` as its central widget — still no `Database`/`QClientModel` include anywhere in this file.

- [ ] **Step 1: Write the failing test**

```cpp
// test/client_mode_window_tests.cpp
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
```

- [ ] **Step 2: Wire the test target and confirm it fails to build**

```cmake
# test/CMakeLists.txt
add_executable(Sessio_client_mode_window_tests client_mode_window_tests.cpp)
target_link_libraries(Sessio_client_mode_window_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Widgets
    Sessio_app
    Sessio_token_client
)
gtest_discover_tests(Sessio_client_mode_window_tests)
```

Run: FAIL — no such constructor.

- [ ] **Step 3: Implement**

```cpp
// src/app/client_mode_window.h
#pragma once

#include "calls_page.h"
#include "device_manager.h"
#include "token_backend_client.h"

#include <QMainWindow>

class ClientModeWindow final : public QMainWindow {
  Q_OBJECT

public:
  ClientModeWindow(pcm::video::DeviceManager *deviceManager,
                   pcm::tokenclient::TokenBackendClient *tokenClient, QWidget *parent = nullptr);
};
```

```cpp
// src/app/client_mode_window.cpp
#include "client_mode_window.h"

ClientModeWindow::ClientModeWindow(pcm::video::DeviceManager *deviceManager,
                                   pcm::tokenclient::TokenBackendClient *tokenClient,
                                   QWidget *parent)
    : QMainWindow(parent) {
  setWindowTitle(tr("Sessio"));
  setCentralWidget(new CallsPage(/*specialistMode=*/false, deviceManager, tokenClient, this));
}
```

- [ ] **Step 4: Re-verify the structural check still passes** (Task 9's grep test) — `database.h`/`qclient_model.h` still never appear in either file.

Run: `ctest --test-dir build -R ClientModeWindow --output-on-failure`
Expected: PASS (both the structural tests and the new functional test)

- [ ] **Step 5: Commit**

```bash
git add src/app/client_mode_window.h src/app/client_mode_window.cpp src/app/CMakeLists.txt test/client_mode_window_tests.cpp test/CMakeLists.txt
git commit -m "feat(app): host CallsPage inside ClientModeWindow"
```

---

### Task 16: `QEventDetailsWidget` "Open Meeting" rewire for LiveKit

**Files:**
- Modify: `src/pages/event_info_page/qevent_details_widget.h`, `.cpp`
- Modify: `src/pages/event_info_page/event_info.h`, `event_info.cpp`
- Modify: `src/app/main_window.h`, `main_window.cpp`
- Modify: `src/app/application.cpp`

**Interfaces:**
- Produces: `QEventDetailsWidget` gains `signals: void openLiveKitMeetingRequested(QString meetingRef);`; `onOpenMeetingClicked()` branches on `mCurrentEvent->providerKind()`; `QEventInfoPage` forwards it as its own `openLiveKitMeetingRequested(QString)` signal from inside `openEventDialog()`/`startCreatingNewEvent()`'s dialog setup; `MainWindow`'s owner (`Application`, Task 17) connects it to `mainWindow->showPage(Pages::calls, ...)` + `callsPage->preselectOwnMeeting(meetingRef)`.

- [ ] **Step 1: Branch `onOpenMeetingClicked()` on provider kind**

```cpp
// qevent_details_widget.h — add
signals:
  void openLiveKitMeetingRequested(QString meetingRef);
```

```cpp
// qevent_details_widget.cpp
void QEventDetailsWidget::onOpenMeetingClicked() {
  if (mCurrentEvent && mCurrentEvent->providerKind() == pcm::meeting::ProviderKind::LiveKit) {
    emit openLiveKitMeetingRequested(mCurrentEvent->meetingRef());
    return;
  }
  pcm::meeting::openMeetingUrl(mMeetingUrlEdit->text(), this);
}
```

- [ ] **Step 2: Forward the signal through `QEventInfoPage`, closing the modal dialog first**

```cpp
// event_info.h — add
signals:
  void openLiveKitMeetingRequested(QString meetingRef);
```

```cpp
// event_info.cpp — in both openEventDialog() and startCreatingNewEvent(),
// right after `detailsWidget->setMeetingCoordinator(mMeetingCoordinator);`:
connect(detailsWidget, &QEventDetailsWidget::openLiveKitMeetingRequested, this,
        [this, &dialog](const QString &meetingRef) {
          dialog.reject();
          emit openLiveKitMeetingRequested(meetingRef);
        });
```

> Implementer note: `&dialog` is a stack-local `QDialog` inside `openEventDialog()`/`startCreatingNewEvent()` — the lambda must not outlive it. Since the connection is made and the dialog is `exec()`'d within the same function call, and `dialog.reject()` is called from within the dialog's own event loop before the function returns, this is safe (identical lifetime pattern to the existing `connect(detailsWidget, ..., &dialog, &QDialog::accept)` calls a few lines below it in this same function).

- [ ] **Step 3: Wire `MainWindow` to switch to the Calls tab**

```cpp
// main_window.h — add
void preselectLiveKitMeeting(const QString &meetingRef);
```

```cpp
// main_window.cpp
void MainWindow::preselectLiveKitMeeting(const QString &meetingRef) {
  showPage(Pages::calls, mBtnCalls);
  if (auto *callsPage = dynamic_cast<CallsPage *>(mPages.value(Pages::calls, nullptr))) {
    callsPage->preselectOwnMeeting(meetingRef);
  }
}
```

```cpp
// application.cpp — connectSignals(), alongside the existing eventInfoPage connections
{
  const auto widget = mMainWindow->getPage(MainWindow::Pages::eventInfo);
  const auto page = dynamic_cast<QEventInfoPage *>(widget);
  connect(page, &QEventInfoPage::openLiveKitMeetingRequested, mMainWindow.get(),
          &MainWindow::preselectLiveKitMeeting);
}
```

- [ ] **Step 4: Manual verification** (no dedicated GoogleTest target for `QEventInfoPage`'s dialog wiring, same as Task 8 — flag as "⚠️ Cannot verify from diff" in the task review package and verify manually)

Run: build via `scripts/run-dev-isolated.sh`, open a LiveKit event created in Task 8's manual check, click "Open Meeting", confirm the app switches to the Звонки tab with that meeting visible/focused rather than trying to open a URL.

- [ ] **Step 5: Commit**

```bash
git add src/pages/event_info_page/qevent_details_widget.h src/pages/event_info_page/qevent_details_widget.cpp src/pages/event_info_page/event_info.h src/pages/event_info_page/event_info.cpp src/app/main_window.h src/app/main_window.cpp src/app/application.cpp
git commit -m "feat(event-info): route LiveKit \"Open Meeting\" to the Calls tab instead of opening a URL"
```

---

### Task 17: `Application` bootstrap — role resolution and window branch

**Files:**
- Modify: `src/app/application.h`, `src/app/application.cpp`

**Interfaces:**
- Consumes: `RoleSelectionDialog` (Task 2), `pcm::config::AppRole` (Task 1), `TokenBackendCredentialStore` (Task 5), `TokenBackendClient` (Task 4), `MainWindow::addCallsPage` (Task 14), `ClientModeWindow` (Task 15).
- Produces: `Application::run()` resolves the role right after config load; on `Client`, constructs only `ClientModeWindow` + the shared video/token pieces and skips `Database`/`QClientModel`/`MeetingCoordinator`/`AutoBackupScheduler`/notifications/app-lock entirely; on `Specialist`, keeps today's full flow and additionally wires `addCallsPage()` plus the notes-panel composition described in Task 14 Step 4.

- [ ] **Step 1: Add role/window members**

```cpp
// application.h — add
#include "app_role.h"
#include "client_mode_window.h"
#include "device_manager.h"
#include "token_backend_client.h"
#include "token_backend_credential_store.h"
// ...
private:
  std::unique_ptr<ClientModeWindow> mClientModeWindow;
  std::unique_ptr<pcm::video::DeviceManager> mDeviceManager;
  std::unique_ptr<pcm::tokenclient::TokenBackendClient> mTokenClient;
  std::unique_ptr<TokenBackendCredentialStore> mTokenCredentialStore;
  QString mBearerCredential; // cached in memory after the async keychain read
  int runSpecialistFlow(QApplication &app);
  int runClientFlow(QApplication &app);
```

- [ ] **Step 2: Resolve the role and branch, right after config/translations are set up and before `restorePendingBackup()`**

```cpp
// application.cpp — Application::run(), replacing the section from
// `restorePendingBackup();` through the final `return app.exec();`
auto conf = pcm::config::Config::read_config();
auto role = pcm::config::appRoleFromString(QString::fromStdString(conf.app_role));
if (!role.has_value() || *role == pcm::config::AppRole::Unset) {
  RoleSelectionDialog roleDialog;
  if (roleDialog.exec() != QDialog::Accepted || !roleDialog.selectedRole().has_value()) {
    return 0; // user closed the first-launch prompt without choosing
  }
  role = roleDialog.selectedRole();
  conf.app_role = pcm::config::appRoleToString(*role).toStdString();
  pcm::config::Config::save_config(conf);
}

mDeviceManager = std::make_unique<pcm::video::DeviceManager>();
mTokenClient =
    std::make_unique<pcm::tokenclient::TokenBackendClient>(QString::fromStdString(conf.token_backend_base_url));
mTokenCredentialStore = std::make_unique<QtKeychainTokenBackendCredentialStore>();

if (*role == pcm::config::AppRole::Client) {
  return runClientFlow(app);
}
return runSpecialistFlow(app);
```

```cpp
// application.cpp — runClientFlow: everything Client mode needs and nothing more
int Application::runClientFlow(QApplication &app) {
  mClientModeWindow = std::make_unique<ClientModeWindow>(mDeviceManager.get(), mTokenClient.get());
  mClientModeWindow->show();
  return app.exec();
}

// runSpecialistFlow: today's body of run() from `restorePendingBackup();`
// onward, unchanged, plus:
int Application::runSpecialistFlow(QApplication &app) {
  restorePendingBackup();
  mDb = std::make_shared<database::Database>(mConf);
  mAutoBackupScheduler = std::make_unique<pcm::backup::AutoBackupScheduler>(mDb);
  mAutoBackupScheduler->start();
  mMeetingCoordinator = std::make_unique<pcm::meeting::MeetingCoordinator>(
      QString::fromStdString(pcm::config::Config::read_config().token_backend_base_url),
      mBearerCredential, this);

  mMainWindow = std::make_unique<MainWindow>();
  mClientModel = std::make_shared<QClientModel>(mDb);

  mMainWindow->addEventInfoPage(new QTimelineModel(mDb, mMeetingCoordinator.get(), this),
                                mMeetingCoordinator.get());
  mMainWindow->addClientInfoPage(mClientModel);
  mMainWindow->addAnalyticsPage(mDb);
  mMainWindow->addClientCardPage(mDb);
  mMainWindow->addClientNotesPage(mDb);
  mMainWindow->addCallsPage(mDeviceManager.get(), mTokenClient.get(),
                            [this]() { return mBearerCredential; });
  mMainWindow->setDatabase(mDb);
  mMainWindow->connectSignals();
  mMainWindow->installEventFilter(this);
  app.installEventFilter(this);
  connectSignals();
  initializeAppLock();
  initializeNotifications();

  mMainWindow->show();
  return app.exec();
}
```

- [ ] **Step 3: Load the cached bearer credential before it's needed**

```cpp
// application.cpp — right after mTokenCredentialStore is constructed, before
// the role branch (both flows may need it: Client mode's CallsPage never
// calls setBearerCredentialProvider, so this is a no-op there, but loading it
// unconditionally keeps the flow simple and the read is cheap/async):
connect(mTokenCredentialStore.get(), &TokenBackendCredentialStore::readFinished, this,
        [this](bool ok, const QString &credential, const QString &) {
          if (ok) {
            mBearerCredential = credential;
          }
        });
mTokenCredentialStore->readBearerCredential();
```

> Implementer note: this read is asynchronous and `MeetingCoordinator`'s construction above reads `mBearerCredential` synchronously afterward — on a cold start, the keychain read may not have completed yet, so `LiveKitMeetingProvider` may be constructed with an empty credential. This is acceptable for this task (the very first LiveKit-provider call after a slow keychain read would then fail with `Unauthorized` and the user retries) rather than blocking application startup on the keychain; flag this as a known, low-severity limitation in the task's self-review rather than solving it here — a synchronous keychain read is out of scope for this plan and would require a different `TokenBackendCredentialStore` API shape.

- [ ] **Step 4: Wire the notes-panel composition for the specialist Calls tab (Task 14 Step 4)**

`CallsPage` (Task 13) already resolves the current call's `eventId` from its cached `UpcomingMeeting` list and emits `eventKnownForCurrentCall(int64_t eventId)` from `startJoin()` — a code/passcode join always has no known event (`mCurrentEventId` is reset before that request), so this signal only ever fires for a specialist's own-meeting join, which is exactly when a notes panel is meaningful. Wire it to the notes panel here:

```cpp
// application.cpp — connectSignals(), after the LiveKit "Open Meeting" wiring from Task 16
{
  auto *callsPage = dynamic_cast<CallsPage *>(mMainWindow->getPage(MainWindow::Pages::calls));
  connect(callsPage, &CallsPage::eventKnownForCurrentCall, this,
          [this, callsPage](const int64_t eventId) {
            const auto client = mDb->get_client_by_event(eventId);
            auto *notesPage = new ClientNotesPage(mDb, callsPage);
            notesPage->setClientInfo(client);
            callsPage->setSidePanelWidget(notesPage);
          });
}
```

`mDb->get_client_by_event(eventId)` is the same call already made a few lines above in this file's existing `provideClientByEventId` handler (`Application::connectSignals()`), so this matches a proven, existing signature rather than a newly-guessed one.

- [ ] **Step 4b: Feed `CallsPage::setUpcomingMeetings()` with the specialist's real events**

`Application` (not `CallsPage`) owns `Database`, so it is also responsible for building the `UpcomingMeeting` list Task 13 defined. Add this alongside the `addCallsPage()` call in `runSpecialistFlow()`:

```cpp
// application.cpp — runSpecialistFlow(), after mMainWindow->addCallsPage(...)
{
  auto *callsPage = dynamic_cast<CallsPage *>(mMainWindow->getPage(MainWindow::Pages::calls));
  const auto nowMs = QDateTime::currentDateTime().toMSecsSinceEpoch();
  const auto endOfDayMs = QDateTime(QDate::currentDate(), QTime(23, 59, 59)).toMSecsSinceEpoch();
  const auto events = mDb->get_upcoming_events(nowMs, endOfDayMs);
  QList<UpcomingMeeting> meetings;
  for (const auto &event : events) {
    if (event.provider_kind != pcm::meeting::providerKindToString(pcm::meeting::ProviderKind::LiveKit)) {
      continue;
    }
    UpcomingMeeting meeting;
    meeting.meetingRef = QString::fromStdString(event.meeting_ref.value_or(""));
    meeting.title = QString::fromStdString(event.name.value_or(""));
    meeting.startTime = QDateTime::fromMSecsSinceEpoch(event.start_date.value_or(0));
    meeting.joinEnabled = true; // refine to a real "within the scheduled window" check if needed
    meeting.eventId = event.id;
    meetings.append(meeting);
  }
  callsPage->setUpcomingMeetings(meetings);
}
```

> Implementer note: this plan has not read `DuckEvent`'s exact field names/types (`provider_kind`, `meeting_ref`, `name`, `start_date`, `id` above are inferred from usage already seen elsewhere in `application.cpp`/`event_item.h`, e.g. `event.client_name`/`event.is_work_event`/`event.start_date` in `notificationBodyForEvent()`). Before writing this code, grep `DuckEvent`'s definition (likely `src/database/...`) and correct any field name that doesn't match; also decide whether this list should refresh periodically (e.g. reuse `mNotificationTimer`'s cadence) or only be built once at startup — this plan does not mandate live refresh, since the spec only asked for a static "today's meetings" list, not a live-updating one.

- [ ] **Step 5: Manual verification**

Run: full app via `scripts/run-dev-isolated.sh` for BOTH roles — delete/rename the test config directory between runs (never the developer's real one) to re-trigger the first-launch `RoleSelectionDialog`, confirm `Specialist` gets the full `MainWindow` with a working Звонки tab and `Client` gets a `ClientModeWindow` with only the join form.

- [ ] **Step 6: Commit**

```bash
git add src/app/application.h src/app/application.cpp src/pages/calls_page/calls_page.h src/pages/calls_page/calls_page.cpp test/calls_page_tests.cpp
git commit -m "feat(app): branch Application bootstrap between MainWindow and ClientModeWindow by AppRole"
```

---

## Part E — `sessio://` protocol

### Task 18: URL parsing and single-instance forwarding

**Files:**
- Create: `src/app/sessio_url.h`, `src/app/sessio_url.cpp`
- Create: `src/app/single_instance_guard.h`, `src/app/single_instance_guard.cpp`
- Modify: `src/app/CMakeLists.txt`, `src/app/application.h`, `src/app/application.cpp`, `src/main.cpp`
- Test: `test/sessio_url_tests.cpp`, `test/single_instance_guard_tests.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  // sessio_url.h
  struct SessioJoinLink { QString code; QString passcode; };
  [[nodiscard]] std::optional<SessioJoinLink> parseSessioJoinUrl(const QString &url);

  // single_instance_guard.h
  class SingleInstanceGuard final : public QObject {
    Q_OBJECT
  public:
    explicit SingleInstanceGuard(QObject *parent = nullptr);
    [[nodiscard]] bool isPrimaryInstance() const; // false if another instance is already running
    void forwardToPrimaryInstance(const QString &url); // call when isPrimaryInstance() is false
  signals:
    void urlReceivedFromSecondaryInstance(QString url);
  };
  ```
  `SingleInstanceGuard` uses `QLocalServer`/`QLocalSocket` on a fixed, well-known local server name (`"Sessio-single-instance"`): the first instance to start `listen()`s successfully and becomes primary; every later instance's `listen()` fails, so it instead connects as a client, writes the raw command-line URL argument (if any) as one line, and exits.

- [ ] **Step 1: Write the failing URL-parsing test**

```cpp
// test/sessio_url_tests.cpp
#include "sessio_url.h"

#include <gtest/gtest.h>

TEST(SessioUrlTest, ParsesCodeAndPasscode) {
  const auto link = parseSessioJoinUrl("sessio://join?code=abc-123&passcode=654321");
  ASSERT_TRUE(link.has_value());
  EXPECT_EQ(link->code, QStringLiteral("abc-123"));
  EXPECT_EQ(link->passcode, QStringLiteral("654321"));
}

TEST(SessioUrlTest, WrongSchemeFailsToParse) {
  EXPECT_FALSE(parseSessioJoinUrl("https://join?code=abc&passcode=1").has_value());
}

TEST(SessioUrlTest, MissingPasscodeFailsToParse) {
  EXPECT_FALSE(parseSessioJoinUrl("sessio://join?code=abc-123").has_value());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

- [ ] **Step 2: Wire the test target and confirm it fails to build**

```cmake
# test/CMakeLists.txt
add_executable(Sessio_sessio_url_tests sessio_url_tests.cpp)
target_link_libraries(Sessio_sessio_url_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_app
)
gtest_discover_tests(Sessio_sessio_url_tests)
```

- [ ] **Step 3: Implement `parseSessioJoinUrl`**

```cpp
// src/app/sessio_url.h
#pragma once

#include <QString>
#include <optional>

struct SessioJoinLink {
  QString code;
  QString passcode;
};

[[nodiscard]] std::optional<SessioJoinLink> parseSessioJoinUrl(const QString &url);
```

```cpp
// src/app/sessio_url.cpp
#include "sessio_url.h"

#include <QUrl>
#include <QUrlQuery>

std::optional<SessioJoinLink> parseSessioJoinUrl(const QString &url) {
  const QUrl parsed(url);
  if (parsed.scheme() != QStringLiteral("sessio") || parsed.host() != QStringLiteral("join")) {
    return std::nullopt;
  }
  const QUrlQuery query(parsed);
  if (!query.hasQueryItem("code") || !query.hasQueryItem("passcode")) {
    return std::nullopt;
  }
  SessioJoinLink link;
  link.code = query.queryItemValue("code");
  link.passcode = query.queryItemValue("passcode");
  return link;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target Sessio_sessio_url_tests && ctest --test-dir build -R SessioUrlTest --output-on-failure`
Expected: PASS (3 tests)

- [ ] **Step 5: Write the failing single-instance test**

```cpp
// test/single_instance_guard_tests.cpp
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
```

- [ ] **Step 6: Wire the test target and confirm it fails to build**

```cmake
# test/CMakeLists.txt
find_package(Qt6 REQUIRED COMPONENTS Network Test)
add_executable(Sessio_single_instance_guard_tests single_instance_guard_tests.cpp)
target_link_libraries(Sessio_single_instance_guard_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Network
    Qt6::Test
    Sessio_app
)
set_target_properties(Sessio_single_instance_guard_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_single_instance_guard_tests)
```

- [ ] **Step 7: Implement `SingleInstanceGuard`**

```cpp
// src/app/single_instance_guard.h
#pragma once

#include <QLocalServer>
#include <QObject>

class SingleInstanceGuard final : public QObject {
  Q_OBJECT

public:
  explicit SingleInstanceGuard(QObject *parent = nullptr);

  [[nodiscard]] bool isPrimaryInstance() const { return mIsPrimary; }
  void forwardToPrimaryInstance(const QString &url);

signals:
  void urlReceivedFromSecondaryInstance(QString url);

private:
  static constexpr auto kServerName = "Sessio-single-instance";

  QLocalServer mServer;
  bool mIsPrimary = false;
};
```

```cpp
// src/app/single_instance_guard.cpp
#include "single_instance_guard.h"

#include <QLocalSocket>

SingleInstanceGuard::SingleInstanceGuard(QObject *parent) : QObject(parent) {
  // A previous crash can leave a stale local-socket file behind; removeServer()
  // is a documented no-op if the name is genuinely still in use, so this only
  // clears a truly abandoned one before listen() decides who's primary.
  QLocalServer::removeServer(kServerName);
  mIsPrimary = mServer.listen(kServerName);

  if (mIsPrimary) {
    connect(&mServer, &QLocalServer::newConnection, this, [this]() {
      auto *socket = mServer.nextPendingConnection();
      connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
        const auto url = QString::fromUtf8(socket->readAll()).trimmed();
        if (!url.isEmpty()) {
          emit urlReceivedFromSecondaryInstance(url);
        }
      });
      connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    });
  }
}

void SingleInstanceGuard::forwardToPrimaryInstance(const QString &url) {
  QLocalSocket socket;
  socket.connectToServer(kServerName);
  if (!socket.waitForConnected(1000)) {
    return;
  }
  socket.write(url.toUtf8());
  socket.waitForBytesWritten(1000);
  socket.disconnectFromServer();
}
```

```cmake
# src/app/CMakeLists.txt — add sessio_url.h/.cpp, single_instance_guard.h/.cpp
# to qt_add_library() sources, and:
find_package(Qt6 REQUIRED COMPONENTS Network)
target_link_libraries(${TARGET_NAME} PUBLIC Qt6::Network)
```

- [ ] **Step 8: Run to verify it passes**

Run: `cmake --build build --target Sessio_single_instance_guard_tests && ctest --test-dir build -R SingleInstanceGuardTest --output-on-failure`
Expected: PASS (2 tests)

- [ ] **Step 9: Wire into `main.cpp`/`Application`**

```cpp
// src/main.cpp
#include "application.h"
#include "config.h"

int main(const int argc, char *argv[]) {
  pcm::config::Config::migrate_legacy_directory();
  const QString launchUrl = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
  return pcm::Application().run(argc, argv, launchUrl);
}
```

```cpp
// application.h — change run()'s signature
int run(int argc, char *argv[], const QString &launchUrl = QString());
// add
private:
  std::unique_ptr<SingleInstanceGuard> mSingleInstanceGuard;
  void handleJoinLink(const QString &url);
```

```cpp
// application.cpp — Application::run(), very first thing (before QApplication
// is even constructed for a secondary instance, to exit fast):
int Application::run(int argc, char *argv[], const QString &launchUrl) {
  mSingleInstanceGuard = std::make_unique<SingleInstanceGuard>();
  if (!mSingleInstanceGuard->isPrimaryInstance()) {
    if (!launchUrl.isEmpty()) {
      mSingleInstanceGuard->forwardToPrimaryInstance(launchUrl);
    }
    return 0;
  }

  QApplication app(argc, argv);
  // ... existing body unchanged ...

  connect(mSingleInstanceGuard.get(), &SingleInstanceGuard::urlReceivedFromSecondaryInstance,
          this, &Application::handleJoinLink);
  if (!launchUrl.isEmpty()) {
    handleJoinLink(launchUrl);
  }

  // ... role branch as in Task 17 ...
}

void Application::handleJoinLink(const QString &url) {
  const auto link = parseSessioJoinUrl(url);
  if (!link.has_value()) {
    return;
  }
  if (mMainWindow) {
    mMainWindow->show();
    mMainWindow->raise();
    if (auto *callsPage = dynamic_cast<CallsPage *>(mMainWindow->getPage(MainWindow::Pages::calls))) {
      callsPage->prefillJoinCode(link->code, link->passcode);
    }
  } else if (mClientModeWindow) {
    mClientModeWindow->show();
    mClientModeWindow->raise();
    // ClientModeWindow's CallsPage is its central widget — reuse the same
    // dynamic_cast pattern via centralWidget() rather than a page lookup.
    if (auto *callsPage = dynamic_cast<CallsPage *>(mClientModeWindow->centralWidget())) {
      callsPage->prefillJoinCode(link->code, link->passcode);
    }
  }
}
```

> Implementer note: on macOS, a custom URL scheme is delivered to a running app via `QFileOpenEvent`, not argv — this is out of scope for this task's cross-platform correctness and is called out explicitly in Task 19, which owns per-OS registration; this task's job is the parsing + single-instance forwarding logic and the Windows/Linux argv path, both of which are fully covered by the tests above.

- [ ] **Step 10: Commit**

```bash
git add src/app/sessio_url.h src/app/sessio_url.cpp src/app/single_instance_guard.h src/app/single_instance_guard.cpp src/app/application.h src/app/application.cpp src/app/CMakeLists.txt src/main.cpp test/sessio_url_tests.cpp test/single_instance_guard_tests.cpp test/CMakeLists.txt
git commit -m "feat(app): parse sessio:// join links and forward them to a single running instance"
```

---

### Task 19: `sessio://` OS-level registration

**Files:**
- Modify: `packaging/windows/` installer script (exact filename depends on what's already there — inspect it first)
- Modify: macOS bundle `Info.plist` generation (`MACOSX_BUNDLE_INFO_PLIST` in `CMakeLists.txt`, or a plist template under `packaging/macos/` if one already exists)
- Modify: `packaging/Sessio.desktop`
- Modify: `src/app/application.cpp` (macOS `QFileOpenEvent` handling)

**Interfaces:** none new — this task is packaging/registration plus one more `eventFilter` case; no unit-testable surface beyond what Task 18 already covers.

- [ ] **Step 1: Handle macOS's `QFileOpenEvent`**

```cpp
// application.cpp — Application::eventFilter(), add a case
bool Application::eventFilter(QObject *watched, QEvent *event) {
  if (event != nullptr && event->type() == QEvent::FileOpen) {
    const auto *openEvent = static_cast<QFileOpenEvent *>(event);
    handleJoinLink(openEvent->url().toString());
    return true;
  }
  // ... existing checks unchanged ...
}
```

> Implementer note: on macOS, `QFileOpenEvent` for a custom URL scheme arrives on the `QApplication` instance itself, so this filter must also be installed on `&app`, not only on `mMainWindow` — confirm `app.installEventFilter(this)` (already present in `run()`) covers it; if `QFileOpenEvent` needs to be caught before `Application::run()` finishes constructing everything (a cold start via a `sessio://` link on macOS), queue it (store the URL, replay via `handleJoinLink` once `mMainWindow`/`mClientModeWindow` exists) rather than dropping it — mirror the existing `launchUrl` parameter's replay-after-construction pattern from Task 18 Step 9.

- [ ] **Step 2: Linux `.desktop` registration**

```ini
# packaging/Sessio.desktop — add
MimeType=x-scheme-handler/sessio;
```

Add a post-install step (wherever this repo already runs `update-desktop-database`/`xdg-mime` for its `.desktop` file installation, or add one to `packaging/` if none exists yet — inspect existing Linux packaging scripts first) running:

```bash
xdg-mime default Sessio.desktop x-scheme-handler/sessio
```

- [ ] **Step 3: Windows registry entry**

Add to the Inno Setup script under `packaging/windows/` (inspect its exact filename and existing `[Registry]` section, if any, before adding to it):

```ini
[Registry]
Root: HKCU; Subkey: "Software\Classes\sessio"; ValueType: string; ValueName: ""; ValueData: "URL:Sessio Protocol"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\sessio"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\sessio\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Sessio.exe"" ""%1"""
```

- [ ] **Step 4: macOS `Info.plist` URL type**

```cmake
# CMakeLists.txt — wherever MACOSX_BUNDLE_INFO_PLIST or bundle properties are
# set for the Sessio target (inspect current macOS packaging setup first; if
# none exists yet, add a template plist under packaging/macos/Info.plist.in
# with the standard qt_add_executable() MACOSX_BUNDLE mechanism) add:
set_target_properties(Sessio PROPERTIES
    MACOSX_BUNDLE_URL_TYPES "sessio"
    MACOSX_BUNDLE_URL_SCHEMES "sessio"
)
```

> Implementer note: `MACOSX_BUNDLE_URL_TYPES`/`MACOSX_BUNDLE_URL_SCHEMES` are illustrative CMake property names — CMake's built-in `MACOSX_BUNDLE_INFO_PLIST` mechanism does not have first-class properties for `CFBundleURLTypes`; the correct, standard approach is a `packaging/macos/Info.plist.in` template with a literal `CFBundleURLTypes` array (containing `CFBundleURLSchemes = ["sessio"]`) passed to `qt_add_executable(... MACOSX_BUNDLE_INFO_PLIST packaging/macos/Info.plist.in)`. This project has no macOS CI runner exercising packaging yet (per `cmake-multi-platform.yml`), so verify by manually inspecting the generated `.app/Contents/Info.plist` after a local macOS build (or ask a teammate with a Mac to verify) rather than trusting an untested CMake property name.

- [ ] **Step 5: Manual verification per platform**

This step cannot be automated in this repo's current CI (no macOS runner; Windows/Linux runners don't install the packaged app). Verify manually on each OS once packaging changes are built: register the protocol (installer run on Windows, `xdg-mime`/desktop-database update on Linux, first launch on macOS registers it automatically via the bundle's `Info.plist`), then click a `sessio://join?code=...&passcode=...` test link from a browser or terminal and confirm Sessio opens (or focuses, if already running) with the Calls tab/`ClientModeWindow` pre-filled.

- [ ] **Step 6: Commit**

```bash
git add packaging/ CMakeLists.txt src/app/application.cpp
git commit -m "feat(packaging): register sessio:// protocol handler on Windows, macOS, and Linux"
```

---

## Part F — Wrap-up

### Task 20: Version bump, changelog, and translations

**Files:**
- Modify: `CMakeLists.txt` (`project(Sessio VERSION ...)`)
- Modify: `src/app/application.cpp` (`app.setApplicationVersion(...)`)
- Modify: `CHANGELOG.md`
- Modify: `translation/app_ru.ts`, `translation/app_en.ts`

**Interfaces:** none — this is process/compliance work required by AGENTS.md for every MR.

- [ ] **Step 1: Bump the version**

```cmake
# CMakeLists.txt
project(Sessio VERSION 0.2.0 LANGUAGES CXX)
```

```cpp
// src/app/application.cpp
app.setApplicationVersion("0.2.0");
```

> Implementer note: confirm the actual next version number against `CHANGELOG.md`'s most recent entry at commit time — this plan was written against `0.1.34`; use whatever is current when this task actually runs, incrementing the minor version (this is a substantial new feature, not a patch).

- [ ] **Step 2: Update the changelog**

```markdown
# CHANGELOG.md — add a new top entry
## [0.2.0]
### Added
- Native in-app LiveKit call UI: a "Звонки" tab in the specialist app and a
  new, minimal client-mode window (`ClientModeWindow`) for joining calls
  without a therapist account.
- `sessio://` deep links for joining a call directly from a shared invitation.
- LiveKit meetings can now be created and canceled from the event editor
  (previously a non-functional stub).
```

- [ ] **Step 3: Sync translations**

Run: `cmake --build build-release --target update_translations`

This regenerates `translation/app_ru.ts` and `translation/app_en.ts` with every new `tr()` string introduced across Tasks 1–19 (`RoleSelectionDialog`, the LiveKit settings section, `CallEntryWidget`, `DeviceCheckWidget`, `CallPage`, the `QEventDetailsWidget` provider-kind labels, etc.) marked `type="unfinished"`.

- [ ] **Step 4: Translate every `type="unfinished"` entry**

Open both `.ts` files and fill in the Russian and English translations for every string this plan introduced (they are all in the code samples above — e.g. "I'm a specialist" → "Я специалист", "Join" → "Войти", "Connecting..." → "Подключение...", etc., matching the Russian copy already used in this feature's own design spec and mockups where applicable).

- [ ] **Step 5: Verify the build fails closed on any remaining unfinished entry**

Run: `cmake --build build-release --parallel`
Expected: succeeds (CI fails the build on any remaining `type="unfinished"` entry per AGENTS.md — this step catches it locally first).

- [ ] **Step 6: Full test suite run**

Run: `cmake -S . -B build -DPCM_BUILD_TESTS=ON && cmake --build build --parallel && ctest --test-dir build --output-on-failure`
Expected: all tests pass, including every new target added across Tasks 1–19.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/app/application.cpp CHANGELOG.md translation/app_ru.ts translation/app_en.ts
git commit -m "chore: bump version to 0.2.0, update changelog and translations for native call UI"
```
