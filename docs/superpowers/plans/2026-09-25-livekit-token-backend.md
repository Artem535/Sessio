# LiveKit Token Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the standalone token backend for issue #78 — a self-contained C++/oat++ HTTP service that mints short-lived, room-scoped LiveKit JWTs for the self-hosted PSY-server LiveKit deployment, without the desktop app ever holding the LiveKit API secret.

**Architecture:** A single statically-built C++ binary (`token-backend/`) that never touches the main `PsyClientManager` desktop build. oat++ provides HTTP routing and request/response DTOs; a thin `MeetingService` holds all business logic and is unit-testable without going through HTTP; a hand-written `sqlite3`-based repository layer persists `accounts`/`meetings`/`invitations`; libsodium provides every cryptographic primitive (HMAC-SHA256 for JWT signing, Argon2id for the room passcode, a fast keyed hash for high-entropy tokens). Deploys as its own podman container on the PSY server, next to (not inside) the existing `livekit`/`meet` containers.

**Tech Stack:** C++20, oat++ 1.3.1 (HTTP + DTO only, via vcpkg), libsodium, plain `sqlite3` (vcpkg), GoogleTest, podman.

## Global Constraints

- Implement `docs/asciidoc/11-token-backend-account-model-adr.adoc` and `docs/asciidoc/12-invitation-security-model-adr.adoc` exactly; do not add ad-hoc crypto or ad-hoc authorization checks outside the `Authorizer` seam.
- Every `meetings`/`invitations` row carries a non-null `account_id` from the first migration, even though exactly one seeded `Account` exists for the MVP.
- No cryptographic primitive is implemented by hand — every hash/HMAC/random-byte call goes through libsodium.
- The room passcode is hashed with `crypto_pwhash_str` (Argon2id), matching the KDF choice already established in `docs/asciidoc/10-encrypted-backup-adr.adoc`. The invitation code and the account bearer credential are high-entropy random tokens hashed with a fast keyed hash (`crypto_generichash`) — no KDF needed for those, and the fast hash allows an exact-match SQL lookup.
- The invitation code stays valid for repeated token exchange until the meeting's scheduled window closes or it is explicitly invalidated — it is never marked "used" after a single redemption.
- The LiveKit API key/secret and the account bearer credential are read only from environment variables at process start; never logged, never written to the database in plaintext.
- The client-role LiveKit identity is deterministic per `meeting_ref` (`"client-" + meeting_ref`), never derived from anything the caller supplies.
- This plan does not implement TLS termination, account self-service registration, billing, or any desktop UI wiring — those are out of scope per the brainstorming session (`docs/video-roadmap.md` §5.6, ADR-11 consequences, and the confirmed TLS deferral).

---

## File Structure

```
token-backend/
├── CMakeLists.txt
├── vcpkg.json
├── Dockerfile
├── README.md
├── src/
│   ├── main.cpp                          — process entry point, DI wiring, --seed-account CLI mode
│   ├── config.h / config.cpp             — env-var configuration
│   ├── crypto/
│   │   ├── random_token.h / .cpp         — random invitation codes, passcodes, bearer credentials
│   │   ├── hashing.h / .cpp              — fast keyed hash (tokens) + Argon2id (passcode)
│   │   └── livekit_jwt.h / .cpp          — LiveKit access-token construction and signing
│   ├── db/
│   │   ├── sqlite_connection.h / .cpp    — thin RAII wrapper over `sqlite3*`
│   │   ├── migrations.h / .cpp           — schema creation
│   │   ├── accounts_repository.h / .cpp
│   │   ├── meetings_repository.h / .cpp
│   │   └── invitations_repository.h / .cpp
│   ├── auth/
│   │   ├── authorizer.h                  — `Authorizer` interface (ADR-11 seam)
│   │   └── static_token_authorizer.h     — header-only
│   ├── service/
│   │   └── meeting_service.h / .cpp      — all business logic, no HTTP/oat++ dependency
│   └── controller/
│       ├── dto.h                         — oat++ request/response DTOs
│       ├── health_controller.h
│       ├── meetings_controller.h
│       └── invitations_controller.h
├── test/
│   ├── CMakeLists.txt
│   ├── hashing_tests.cpp
│   ├── livekit_jwt_tests.cpp
│   ├── sqlite_connection_tests.cpp
│   ├── accounts_repository_tests.cpp
│   ├── meetings_repository_tests.cpp
│   ├── invitations_repository_tests.cpp
│   ├── static_token_authorizer_tests.cpp
│   └── meeting_service_tests.cpp
└── scripts/
    └── deploy-note.md                    — podman-compose snippet for the PSY server
```

### Task 1: Project scaffold and health endpoint

**Files:**
- Create: `token-backend/CMakeLists.txt`
- Create: `token-backend/vcpkg.json`
- Create: `token-backend/src/controller/health_controller.h`
- Create: `token-backend/src/main.cpp`
- Create: `token-backend/test/CMakeLists.txt`
- Create: `token-backend/test/health_controller_tests.cpp`

**Interfaces:**
- Produces: `pcm::tokenbackend::HealthController` — `oatpp::web::server::api::ApiController` subclass with one endpoint, `GET /healthz`, and a plain method `std::string statusText()` the test calls directly (no live HTTP needed for the test).

- [ ] **Step 1: Write `vcpkg.json`**

```json
{
  "name": "pcm-token-backend",
  "version": "0.1.0",
  "dependencies": [
    "oatpp",
    "libsodium",
    "sqlite3",
    "gtest"
  ]
}
```

- [ ] **Step 2: Write the top-level `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(pcm_token_backend LANGUAGES CXX)

find_package(oatpp REQUIRED)
find_package(unofficial-sodium REQUIRED)
find_package(unofficial-sqlite3 REQUIRED)

option(PCM_TB_BUILD_TESTS "Build token backend tests" ON)

add_library(token_backend_lib STATIC
  src/controller/health_controller.h
)
set_target_properties(token_backend_lib PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(token_backend_lib PUBLIC src)
target_link_libraries(token_backend_lib PUBLIC
  oatpp::oatpp
  unofficial-sodium::sodium
  unofficial-sqlite3::sqlite3
)

add_executable(pcm_token_backend src/main.cpp)
target_link_libraries(pcm_token_backend PRIVATE token_backend_lib)

if(PCM_TB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()
```

- [ ] **Step 3: Write the failing health controller test**

```cpp
// token-backend/test/health_controller_tests.cpp
#include "controller/health_controller.h"

#include <gtest/gtest.h>

TEST(HealthControllerTest, StatusTextReportsOk) {
  pcm::tokenbackend::HealthController controller;
  EXPECT_EQ(controller.statusText(), "ok");
}
```

- [ ] **Step 4: Write `test/CMakeLists.txt`**

```cmake
find_package(GTest REQUIRED)

add_executable(token_backend_tests
  health_controller_tests.cpp
)
target_link_libraries(token_backend_tests PRIVATE
  token_backend_lib
  GTest::gtest
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(token_backend_tests)
```

- [ ] **Step 5: Run the test to verify it fails**

Run: `cmake -S token-backend -B token-backend/build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake && cmake --build token-backend/build`
Expected: FAIL — `controller/health_controller.h` does not exist yet.

- [ ] **Step 6: Write `src/controller/health_controller.h`**

```cpp
#pragma once

#include "oatpp/web/server/api/ApiController.hpp"
#include "oatpp/core/macro/codegen.hpp"
#include "oatpp/core/macro/component.hpp"

namespace pcm::tokenbackend {

#include OATPP_CODEGEN_BEGIN(ApiController)

class HealthController : public oatpp::web::server::api::ApiController {
public:
  HealthController()
      : oatpp::web::server::api::ApiController(
            oatpp::base::Environment::getComponent<
                std::shared_ptr<oatpp::web::mime::ContentMappers>>()) {}

  std::string statusText() const { return "ok"; }

  ENDPOINT("GET", "/healthz", getHealth) {
    return createResponse(Status::CODE_200, statusText());
  }
};

#include OATPP_CODEGEN_END(ApiController)

} // namespace pcm::tokenbackend
```

- [ ] **Step 7: Build and run the test to verify it passes**

Run: `cmake --build token-backend/build && ctest --test-dir token-backend/build -R HealthControllerTest --output-on-failure`
Expected: PASS

- [ ] **Step 8: Write `src/main.cpp` with a minimal running server**

```cpp
// token-backend/src/main.cpp
#include "controller/health_controller.h"

#include "oatpp/network/Server.hpp"
#include "oatpp/network/tcp/server/ConnectionProvider.hpp"
#include "oatpp/web/server/HttpConnectionHandler.hpp"
#include "oatpp/web/server/HttpRouter.hpp"
#include "oatpp/parser/json/mapping/ObjectMapper.hpp"

#include <cstdlib>
#include <iostream>

int main(int argc, char **argv) {
  oatpp::base::Environment::init();

  {
    auto router = oatpp::web::server::HttpRouter::createShared();

    auto objectMapper = oatpp::parser::json::mapping::ObjectMapper::createShared();
    auto mappers = std::make_shared<oatpp::web::mime::ContentMappers>();
    mappers->putMapper(objectMapper);
    oatpp::base::Environment::getComponents().add(mappers);

    pcm::tokenbackend::HealthController healthController;
    healthController.addEndpointsToRouter(router);

    auto connectionHandler = oatpp::web::server::HttpConnectionHandler::createShared(router);
    const char *portEnv = std::getenv("PORT");
    v_uint16 port = portEnv ? static_cast<v_uint16>(std::atoi(portEnv)) : 8080;
    auto connectionProvider =
        oatpp::network::tcp::server::ConnectionProvider::createShared({"0.0.0.0", port});

    oatpp::network::Server server(connectionProvider, connectionHandler);
    std::cout << "pcm-token-backend listening on :" << port << std::endl;
    server.run();
  }

  oatpp::base::Environment::destroy();
  return 0;
}
```

- [ ] **Step 9: Commit**

```bash
git add token-backend/
git commit -m "feat(token-backend): scaffold oat++ service with health endpoint"
```

---

### Task 2: Configuration and SQLite schema

**Files:**
- Create: `token-backend/src/config.h`, `token-backend/src/config.cpp`
- Create: `token-backend/src/db/sqlite_connection.h`, `token-backend/src/db/sqlite_connection.cpp`
- Create: `token-backend/src/db/migrations.h`, `token-backend/src/db/migrations.cpp`
- Create: `token-backend/test/sqlite_connection_tests.cpp`
- Modify: `token-backend/CMakeLists.txt`, `token-backend/test/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  namespace pcm::tokenbackend {
  struct Config {
    uint16_t port;
    std::string dbPath;
    std::string liveKitApiKey;
    std::string liveKitApiSecret;
    int tokenTtlSeconds;
    static Config fromEnv(); // throws std::runtime_error if a required var is missing
  };

  class SqliteConnection {
  public:
    explicit SqliteConnection(const std::string &path);
    ~SqliteConnection();
    SqliteConnection(const SqliteConnection &) = delete;
    SqliteConnection &operator=(const SqliteConnection &) = delete;
    sqlite3 *raw() const;
    void exec(const std::string &sql); // throws std::runtime_error on failure
  private:
    sqlite3 *db_ = nullptr;
  };

  void runMigrations(SqliteConnection &conn);
  }
  ```

- [ ] **Step 1: Write the failing config test inline in a new file, then the sqlite connection test**

```cpp
// token-backend/test/sqlite_connection_tests.cpp
#include "db/sqlite_connection.h"
#include "db/migrations.h"

#include <gtest/gtest.h>
#include <sqlite3.h>

TEST(SqliteConnectionTest, ExecRunsStatement) {
  pcm::tokenbackend::SqliteConnection conn(":memory:");
  conn.exec("CREATE TABLE t (id INTEGER PRIMARY KEY)");
  conn.exec("INSERT INTO t (id) VALUES (1)");

  sqlite3_stmt *stmt = nullptr;
  sqlite3_prepare_v2(conn.raw(), "SELECT COUNT(*) FROM t", -1, &stmt, nullptr);
  sqlite3_step(stmt);
  EXPECT_EQ(sqlite3_column_int(stmt, 0), 1);
  sqlite3_finalize(stmt);
}

TEST(MigrationsTest, CreatesAllThreeTables) {
  pcm::tokenbackend::SqliteConnection conn(":memory:");
  pcm::tokenbackend::runMigrations(conn);

  for (const std::string &table : {"accounts", "meetings", "invitations"}) {
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(conn.raw(),
                        "SELECT name FROM sqlite_master WHERE type='table' AND name=?", -1,
                        &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, table.c_str(), -1, SQLITE_TRANSIENT);
    EXPECT_EQ(sqlite3_step(stmt), SQLITE_ROW) << "missing table " << table;
    sqlite3_finalize(stmt);
  }
}
```

- [ ] **Step 2: Add the new test file to `test/CMakeLists.txt`**

```cmake
add_executable(token_backend_tests
  health_controller_tests.cpp
  sqlite_connection_tests.cpp
)
target_link_libraries(token_backend_tests PRIVATE
  token_backend_lib
  GTest::gtest
  GTest::gtest_main
)
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `cmake --build token-backend/build`
Expected: FAIL — `db/sqlite_connection.h` and `db/migrations.h` do not exist.

- [ ] **Step 4: Write `src/db/sqlite_connection.h`**

```cpp
#pragma once

#include <sqlite3.h>
#include <string>

namespace pcm::tokenbackend {

class SqliteConnection {
public:
  explicit SqliteConnection(const std::string &path);
  ~SqliteConnection();

  SqliteConnection(const SqliteConnection &) = delete;
  SqliteConnection &operator=(const SqliteConnection &) = delete;

  sqlite3 *raw() const { return db_; }
  void exec(const std::string &sql);

private:
  sqlite3 *db_ = nullptr;
};

} // namespace pcm::tokenbackend
```

- [ ] **Step 5: Write `src/db/sqlite_connection.cpp`**

```cpp
#include "db/sqlite_connection.h"

#include <stdexcept>

namespace pcm::tokenbackend {

SqliteConnection::SqliteConnection(const std::string &path) {
  if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
    std::string message = sqlite3_errmsg(db_);
    sqlite3_close(db_);
    throw std::runtime_error("failed to open sqlite database: " + message);
  }
  sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
}

SqliteConnection::~SqliteConnection() {
  if (db_) {
    sqlite3_close(db_);
  }
}

void SqliteConnection::exec(const std::string &sql) {
  char *errMsg = nullptr;
  if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
    std::string message = errMsg ? errMsg : "unknown sqlite error";
    sqlite3_free(errMsg);
    throw std::runtime_error("sqlite exec failed: " + message);
  }
}

} // namespace pcm::tokenbackend
```

- [ ] **Step 6: Write `src/db/migrations.h` and `src/db/migrations.cpp`**

```cpp
// token-backend/src/db/migrations.h
#pragma once

#include "db/sqlite_connection.h"

namespace pcm::tokenbackend {
void runMigrations(SqliteConnection &conn);
} // namespace pcm::tokenbackend
```

```cpp
// token-backend/src/db/migrations.cpp
#include "db/migrations.h"

namespace pcm::tokenbackend {

void runMigrations(SqliteConnection &conn) {
  conn.exec(R"sql(
    CREATE TABLE IF NOT EXISTS accounts (
      id INTEGER PRIMARY KEY,
      credential_hash TEXT NOT NULL UNIQUE,
      created_at TEXT NOT NULL
    );
  )sql");

  conn.exec(R"sql(
    CREATE TABLE IF NOT EXISTS meetings (
      id INTEGER PRIMARY KEY,
      meeting_ref TEXT NOT NULL UNIQUE,
      account_id INTEGER NOT NULL REFERENCES accounts(id),
      room_name TEXT NOT NULL UNIQUE,
      scheduled_start TEXT NOT NULL,
      scheduled_end TEXT NOT NULL,
      status TEXT NOT NULL DEFAULT 'active',
      created_at TEXT NOT NULL
    );
  )sql");

  conn.exec(R"sql(
    CREATE TABLE IF NOT EXISTS invitations (
      id INTEGER PRIMARY KEY,
      meeting_id INTEGER NOT NULL REFERENCES meetings(id),
      account_id INTEGER NOT NULL REFERENCES accounts(id),
      invitation_code_hash TEXT NOT NULL UNIQUE,
      passcode_hash TEXT NOT NULL,
      passcode_attempts INTEGER NOT NULL DEFAULT 0,
      status TEXT NOT NULL DEFAULT 'active',
      created_at TEXT NOT NULL
    );
  )sql");
}

} // namespace pcm::tokenbackend
```

- [ ] **Step 7: Write `src/config.h` and `src/config.cpp`**

```cpp
// token-backend/src/config.h
#pragma once

#include <cstdint>
#include <string>

namespace pcm::tokenbackend {

struct Config {
  uint16_t port;
  std::string dbPath;
  std::string liveKitApiKey;
  std::string liveKitApiSecret;
  int tokenTtlSeconds;

  static Config fromEnv();
};

} // namespace pcm::tokenbackend
```

```cpp
// token-backend/src/config.cpp
#include "config.h"

#include <cstdlib>
#include <stdexcept>

namespace pcm::tokenbackend {

namespace {
std::string requireEnv(const char *name) {
  const char *value = std::getenv(name);
  if (!value || value[0] == '\0') {
    throw std::runtime_error(std::string("missing required environment variable ") + name);
  }
  return value;
}
} // namespace

Config Config::fromEnv() {
  Config config;
  const char *portEnv = std::getenv("PORT");
  config.port = portEnv ? static_cast<uint16_t>(std::atoi(portEnv)) : 8080;

  const char *dbPathEnv = std::getenv("DB_PATH");
  config.dbPath = dbPathEnv ? dbPathEnv : "token-backend.sqlite3";

  config.liveKitApiKey = requireEnv("LIVEKIT_API_KEY");
  config.liveKitApiSecret = requireEnv("LIVEKIT_API_SECRET");

  const char *ttlEnv = std::getenv("TOKEN_TTL_SECONDS");
  config.tokenTtlSeconds = ttlEnv ? std::atoi(ttlEnv) : 600;

  return config;
}

} // namespace pcm::tokenbackend
```

- [ ] **Step 8: Update `CMakeLists.txt` to compile the new sources**

```cmake
add_library(token_backend_lib STATIC
  src/controller/health_controller.h
  src/config.cpp
  src/db/sqlite_connection.cpp
  src/db/migrations.cpp
)
```

- [ ] **Step 9: Build and run tests to verify they pass**

Run: `cmake --build token-backend/build && ctest --test-dir token-backend/build --output-on-failure`
Expected: PASS

- [ ] **Step 10: Commit**

```bash
git add token-backend/
git commit -m "feat(token-backend): add env config, sqlite connection, and schema migrations"
```

---

### Task 3: Crypto helpers — random tokens and hashing

**Files:**
- Create: `token-backend/src/crypto/random_token.h`, `token-backend/src/crypto/random_token.cpp`
- Create: `token-backend/src/crypto/hashing.h`, `token-backend/src/crypto/hashing.cpp`
- Create: `token-backend/test/hashing_tests.cpp`
- Modify: `token-backend/CMakeLists.txt`, `token-backend/test/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  namespace pcm::tokenbackend {
  std::string generateUrlSafeToken(size_t numRandomBytes); // base64url, no padding
  std::string generateNumericPasscode();                   // 6 ASCII digits, zero-padded

  std::string fastHash(const std::string &plaintext);           // deterministic, hex-encoded
  bool fastHashMatches(const std::string &plaintext, const std::string &storedHashHex);

  std::string hashPasscode(const std::string &passcode);         // Argon2id, crypto_pwhash_str
  bool passcodeMatches(const std::string &passcode, const std::string &storedHash);
  }
  ```

- [ ] **Step 1: Write the failing hashing/token tests**

```cpp
// token-backend/test/hashing_tests.cpp
#include "crypto/hashing.h"
#include "crypto/random_token.h"

#include <gtest/gtest.h>
#include <sodium.h>

class CryptoTest : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(sodium_init() >= 0, true); }
};

TEST_F(CryptoTest, GenerateUrlSafeTokenHasNoPaddingOrSlashes) {
  auto token = pcm::tokenbackend::generateUrlSafeToken(24);
  EXPECT_FALSE(token.empty());
  EXPECT_EQ(token.find('='), std::string::npos);
  EXPECT_EQ(token.find('/'), std::string::npos);
  EXPECT_EQ(token.find('+'), std::string::npos);
}

TEST_F(CryptoTest, GenerateNumericPasscodeIsSixDigits) {
  auto passcode = pcm::tokenbackend::generateNumericPasscode();
  ASSERT_EQ(passcode.size(), 6u);
  for (char c : passcode) {
    EXPECT_TRUE(c >= '0' && c <= '9');
  }
}

TEST_F(CryptoTest, FastHashIsDeterministic) {
  auto a = pcm::tokenbackend::fastHash("same-input");
  auto b = pcm::tokenbackend::fastHash("same-input");
  EXPECT_EQ(a, b);
}

TEST_F(CryptoTest, FastHashMatchesVerifiesCorrectly) {
  auto hash = pcm::tokenbackend::fastHash("token-value");
  EXPECT_TRUE(pcm::tokenbackend::fastHashMatches("token-value", hash));
  EXPECT_FALSE(pcm::tokenbackend::fastHashMatches("wrong-value", hash));
}

TEST_F(CryptoTest, PasscodeHashRoundTrips) {
  auto hash = pcm::tokenbackend::hashPasscode("123456");
  EXPECT_TRUE(pcm::tokenbackend::passcodeMatches("123456", hash));
  EXPECT_FALSE(pcm::tokenbackend::passcodeMatches("000000", hash));
}

TEST_F(CryptoTest, PasscodeHashIsRandomizedPerCall) {
  auto first = pcm::tokenbackend::hashPasscode("123456");
  auto second = pcm::tokenbackend::hashPasscode("123456");
  EXPECT_NE(first, second) << "Argon2id must salt each call independently";
}
```

- [ ] **Step 2: Add the test file to `test/CMakeLists.txt`**

```cmake
add_executable(token_backend_tests
  health_controller_tests.cpp
  sqlite_connection_tests.cpp
  hashing_tests.cpp
)
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `cmake --build token-backend/build`
Expected: FAIL — `crypto/hashing.h` and `crypto/random_token.h` do not exist.

- [ ] **Step 4: Write `src/crypto/random_token.h` and `.cpp`**

```cpp
// token-backend/src/crypto/random_token.h
#pragma once

#include <string>

namespace pcm::tokenbackend {
std::string generateUrlSafeToken(size_t numRandomBytes);
std::string generateNumericPasscode();
} // namespace pcm::tokenbackend
```

```cpp
// token-backend/src/crypto/random_token.cpp
#include "crypto/random_token.h"

#include <sodium.h>

#include <iomanip>
#include <sstream>
#include <vector>

namespace pcm::tokenbackend {

namespace {
constexpr char kBase64UrlAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
}

std::string generateUrlSafeToken(size_t numRandomBytes) {
  std::vector<unsigned char> bytes(numRandomBytes);
  randombytes_buf(bytes.data(), bytes.size());

  std::string out;
  out.reserve((numRandomBytes * 4 + 2) / 3);
  size_t i = 0;
  while (i + 3 <= bytes.size()) {
    uint32_t triple = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
    out += kBase64UrlAlphabet[(triple >> 18) & 0x3F];
    out += kBase64UrlAlphabet[(triple >> 12) & 0x3F];
    out += kBase64UrlAlphabet[(triple >> 6) & 0x3F];
    out += kBase64UrlAlphabet[triple & 0x3F];
    i += 3;
  }
  size_t remaining = bytes.size() - i;
  if (remaining == 1) {
    uint32_t val = bytes[i] << 16;
    out += kBase64UrlAlphabet[(val >> 18) & 0x3F];
    out += kBase64UrlAlphabet[(val >> 12) & 0x3F];
  } else if (remaining == 2) {
    uint32_t val = (bytes[i] << 16) | (bytes[i + 1] << 8);
    out += kBase64UrlAlphabet[(val >> 18) & 0x3F];
    out += kBase64UrlAlphabet[(val >> 12) & 0x3F];
    out += kBase64UrlAlphabet[(val >> 6) & 0x3F];
  }
  return out;
}

std::string generateNumericPasscode() {
  uint32_t value = randombytes_uniform(1000000);
  std::ostringstream oss;
  oss << std::setw(6) << std::setfill('0') << value;
  return oss.str();
}

} // namespace pcm::tokenbackend
```

- [ ] **Step 5: Write `src/crypto/hashing.h` and `.cpp`**

```cpp
// token-backend/src/crypto/hashing.h
#pragma once

#include <string>

namespace pcm::tokenbackend {

std::string fastHash(const std::string &plaintext);
bool fastHashMatches(const std::string &plaintext, const std::string &storedHashHex);

std::string hashPasscode(const std::string &passcode);
bool passcodeMatches(const std::string &passcode, const std::string &storedHash);

} // namespace pcm::tokenbackend
```

```cpp
// token-backend/src/crypto/hashing.cpp
#include "crypto/hashing.h"

#include <sodium.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace pcm::tokenbackend {

std::string fastHash(const std::string &plaintext) {
  unsigned char out[crypto_generichash_BYTES];
  crypto_generichash(out, sizeof(out),
                      reinterpret_cast<const unsigned char *>(plaintext.data()),
                      plaintext.size(), nullptr, 0);

  std::ostringstream oss;
  for (unsigned char byte : out) {
    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
  }
  return oss.str();
}

bool fastHashMatches(const std::string &plaintext, const std::string &storedHashHex) {
  auto computed = fastHash(plaintext);
  if (computed.size() != storedHashHex.size()) {
    return false;
  }
  return sodium_memcmp(computed.data(), storedHashHex.data(), computed.size()) == 0;
}

std::string hashPasscode(const std::string &passcode) {
  char out[crypto_pwhash_STRBYTES];
  if (crypto_pwhash_str(out, passcode.c_str(), passcode.size(),
                         crypto_pwhash_OPSLIMIT_MODERATE,
                         crypto_pwhash_MEMLIMIT_MODERATE) != 0) {
    throw std::runtime_error("passcode hashing failed (out of memory)");
  }
  return std::string(out);
}

bool passcodeMatches(const std::string &passcode, const std::string &storedHash) {
  return crypto_pwhash_str_verify(storedHash.c_str(), passcode.c_str(), passcode.size()) == 0;
}

} // namespace pcm::tokenbackend
```

- [ ] **Step 6: Update `CMakeLists.txt`**

```cmake
add_library(token_backend_lib STATIC
  src/controller/health_controller.h
  src/config.cpp
  src/db/sqlite_connection.cpp
  src/db/migrations.cpp
  src/crypto/random_token.cpp
  src/crypto/hashing.cpp
)
```

- [ ] **Step 7: Build and run tests to verify they pass**

Run: `cmake --build token-backend/build && ctest --test-dir token-backend/build --output-on-failure`
Expected: PASS

- [ ] **Step 8: Commit**

```bash
git add token-backend/
git commit -m "feat(token-backend): add libsodium-backed random tokens and hashing"
```

---

### Task 4: LiveKit JWT signer

**Files:**
- Create: `token-backend/src/crypto/livekit_jwt.h`, `token-backend/src/crypto/livekit_jwt.cpp`
- Create: `token-backend/test/livekit_jwt_tests.cpp`
- Modify: `token-backend/CMakeLists.txt`, `token-backend/test/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from earlier tasks (libsodium only).
- Produces:
  ```cpp
  namespace pcm::tokenbackend {
  struct VideoGrants {
    std::string room;
    bool roomJoin = true;
    bool canPublish = true;
    bool canSubscribe = true;
    bool canPublishData = true;
  };

  std::string mintLiveKitJwt(const std::string &apiKey, const std::string &apiSecret,
                              const std::string &identity, const VideoGrants &grants,
                              int ttlSeconds);
  // throws std::invalid_argument if apiKey/apiSecret/identity/grants.room is empty
  }
  ```

- [ ] **Step 1: Write the failing JWT tests**

```cpp
// token-backend/test/livekit_jwt_tests.cpp
#include "crypto/livekit_jwt.h"

#include <gtest/gtest.h>
#include <sodium.h>

#include <sstream>

namespace {

std::string base64UrlDecode(const std::string &input) {
  static const std::string alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::vector<int> table(256, -1);
  for (size_t i = 0; i < alphabet.size(); ++i) {
    table[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
  }
  std::string out;
  int buffer = 0, bitsCollected = 0;
  for (char c : input) {
    int value = table[static_cast<unsigned char>(c)];
    if (value < 0) continue;
    buffer = (buffer << 6) | value;
    bitsCollected += 6;
    if (bitsCollected >= 8) {
      bitsCollected -= 8;
      out += static_cast<char>((buffer >> bitsCollected) & 0xFF);
    }
  }
  return out;
}

std::vector<std::string> splitJwt(const std::string &jwt) {
  std::vector<std::string> parts;
  std::stringstream ss(jwt);
  std::string part;
  while (std::getline(ss, part, '.')) {
    parts.push_back(part);
  }
  return parts;
}

} // namespace

class LiveKitJwtTest : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(sodium_init() >= 0, true); }
};

TEST_F(LiveKitJwtTest, HasThreeDotSeparatedParts) {
  pcm::tokenbackend::VideoGrants grants;
  grants.room = "rm_test";
  auto jwt = pcm::tokenbackend::mintLiveKitJwt("api-key", "api-secret", "identity-1", grants, 60);
  EXPECT_EQ(splitJwt(jwt).size(), 3u);
}

TEST_F(LiveKitJwtTest, PayloadContainsExpectedClaims) {
  pcm::tokenbackend::VideoGrants grants;
  grants.room = "rm_9f7e21";
  auto jwt = pcm::tokenbackend::mintLiveKitJwt("my-key", "my-secret", "client-mtg_a1b2c3", grants, 60);
  auto parts = splitJwt(jwt);
  auto payload = base64UrlDecode(parts[1]);

  EXPECT_NE(payload.find("\"iss\":\"my-key\""), std::string::npos);
  EXPECT_NE(payload.find("\"sub\":\"client-mtg_a1b2c3\""), std::string::npos);
  EXPECT_NE(payload.find("\"room\":\"rm_9f7e21\""), std::string::npos);
  EXPECT_NE(payload.find("\"roomJoin\":true"), std::string::npos);
}

TEST_F(LiveKitJwtTest, SignatureVerifiesWithCorrectSecret) {
  pcm::tokenbackend::VideoGrants grants;
  grants.room = "rm_test";
  auto jwt = pcm::tokenbackend::mintLiveKitJwt("api-key", "correct-secret", "identity-1", grants, 60);
  auto parts = splitJwt(jwt);
  std::string signingInput = parts[0] + "." + parts[1];

  std::string secret = "correct-secret";
  unsigned char expected[crypto_auth_hmacsha256_BYTES];
  crypto_auth_hmacsha256_state state;
  crypto_auth_hmacsha256_init(&state, reinterpret_cast<const unsigned char *>(secret.data()),
                               secret.size());
  crypto_auth_hmacsha256_update(&state,
                                 reinterpret_cast<const unsigned char *>(signingInput.data()),
                                 signingInput.size());
  crypto_auth_hmacsha256_final(&state, expected);

  auto actualSig = base64UrlDecode(parts[2]);
  ASSERT_EQ(actualSig.size(), sizeof(expected));
  EXPECT_EQ(0, sodium_memcmp(expected, actualSig.data(), sizeof(expected)));
}

TEST_F(LiveKitJwtTest, RejectsEmptyRoom) {
  pcm::tokenbackend::VideoGrants grants;
  grants.room = "";
  EXPECT_THROW(pcm::tokenbackend::mintLiveKitJwt("k", "s", "identity", grants, 60),
               std::invalid_argument);
}
```

- [ ] **Step 2: Add the test file to `test/CMakeLists.txt`**

```cmake
add_executable(token_backend_tests
  health_controller_tests.cpp
  sqlite_connection_tests.cpp
  hashing_tests.cpp
  livekit_jwt_tests.cpp
)
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `cmake --build token-backend/build`
Expected: FAIL — `crypto/livekit_jwt.h` does not exist.

- [ ] **Step 4: Write `src/crypto/livekit_jwt.h`**

```cpp
#pragma once

#include <string>

namespace pcm::tokenbackend {

struct VideoGrants {
  std::string room;
  bool roomJoin = true;
  bool canPublish = true;
  bool canSubscribe = true;
  bool canPublishData = true;
};

std::string mintLiveKitJwt(const std::string &apiKey, const std::string &apiSecret,
                            const std::string &identity, const VideoGrants &grants,
                            int ttlSeconds);

} // namespace pcm::tokenbackend
```

- [ ] **Step 5: Write `src/crypto/livekit_jwt.cpp`**

```cpp
#include "crypto/livekit_jwt.h"

#include <sodium.h>

#include <chrono>
#include <sstream>
#include <stdexcept>

namespace pcm::tokenbackend {

namespace {

constexpr char kBase64UrlAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64UrlEncode(const unsigned char *data, size_t len) {
  std::string out;
  out.reserve((len * 4 + 2) / 3);
  size_t i = 0;
  while (i + 3 <= len) {
    uint32_t triple = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
    out += kBase64UrlAlphabet[(triple >> 18) & 0x3F];
    out += kBase64UrlAlphabet[(triple >> 12) & 0x3F];
    out += kBase64UrlAlphabet[(triple >> 6) & 0x3F];
    out += kBase64UrlAlphabet[triple & 0x3F];
    i += 3;
  }
  size_t remaining = len - i;
  if (remaining == 1) {
    uint32_t val = data[i] << 16;
    out += kBase64UrlAlphabet[(val >> 18) & 0x3F];
    out += kBase64UrlAlphabet[(val >> 12) & 0x3F];
  } else if (remaining == 2) {
    uint32_t val = (data[i] << 16) | (data[i + 1] << 8);
    out += kBase64UrlAlphabet[(val >> 18) & 0x3F];
    out += kBase64UrlAlphabet[(val >> 12) & 0x3F];
    out += kBase64UrlAlphabet[(val >> 6) & 0x3F];
  }
  return out;
}

std::string base64UrlEncode(const std::string &s) {
  return base64UrlEncode(reinterpret_cast<const unsigned char *>(s.data()), s.size());
}

std::string jsonEscape(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += c;
  }
  return out;
}

} // namespace

std::string mintLiveKitJwt(const std::string &apiKey, const std::string &apiSecret,
                            const std::string &identity, const VideoGrants &grants,
                            int ttlSeconds) {
  if (apiKey.empty() || apiSecret.empty() || identity.empty() || grants.room.empty()) {
    throw std::invalid_argument("apiKey, apiSecret, identity, and grants.room are required");
  }

  auto now = std::chrono::system_clock::now();
  auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  auto expSeconds = nowSeconds + ttlSeconds;

  std::ostringstream header;
  header << R"({"alg":"HS256","typ":"JWT"})";

  std::ostringstream payload;
  payload << "{"
          << "\"iss\":\"" << jsonEscape(apiKey) << "\","
          << "\"sub\":\"" << jsonEscape(identity) << "\","
          << "\"nbf\":" << nowSeconds << ","
          << "\"exp\":" << expSeconds << ","
          << "\"video\":{"
          << "\"room\":\"" << jsonEscape(grants.room) << "\","
          << "\"roomJoin\":" << (grants.roomJoin ? "true" : "false") << ","
          << "\"canPublish\":" << (grants.canPublish ? "true" : "false") << ","
          << "\"canSubscribe\":" << (grants.canSubscribe ? "true" : "false") << ","
          << "\"canPublishData\":" << (grants.canPublishData ? "true" : "false")
          << "}"
          << "}";

  std::string signingInput = base64UrlEncode(header.str()) + "." + base64UrlEncode(payload.str());

  // crypto_auth_hmacsha256() (the one-shot form) requires an exactly
  // crypto_auth_hmacsha256_KEYBYTES (32-byte) key and reads out of bounds on
  // anything shorter — LiveKit API secrets are arbitrary-length strings, so
  // the stateful init/update/final API is used instead: it implements real
  // HMAC key handling (short keys zero-padded, long keys pre-hashed) for any
  // key length, matching what a standard HMAC-SHA256 library would produce.
  unsigned char signature[crypto_auth_hmacsha256_BYTES];
  crypto_auth_hmacsha256_state state;
  crypto_auth_hmacsha256_init(&state, reinterpret_cast<const unsigned char *>(apiSecret.data()),
                               apiSecret.size());
  crypto_auth_hmacsha256_update(&state,
                                 reinterpret_cast<const unsigned char *>(signingInput.data()),
                                 signingInput.size());
  crypto_auth_hmacsha256_final(&state, signature);

  return signingInput + "." + base64UrlEncode(signature, sizeof(signature));
}

} // namespace pcm::tokenbackend
```

- [ ] **Step 6: Update `CMakeLists.txt`**

```cmake
add_library(token_backend_lib STATIC
  src/controller/health_controller.h
  src/config.cpp
  src/db/sqlite_connection.cpp
  src/db/migrations.cpp
  src/crypto/random_token.cpp
  src/crypto/hashing.cpp
  src/crypto/livekit_jwt.cpp
)
```

- [ ] **Step 7: Build and run tests to verify they pass**

Run: `cmake --build token-backend/build && ctest --test-dir token-backend/build --output-on-failure`
Expected: PASS

- [ ] **Step 8: Commit**

```bash
git add token-backend/
git commit -m "feat(token-backend): mint and sign LiveKit access tokens"
```

---

### Task 5: Accounts repository and the Authorizer seam

**Files:**
- Create: `token-backend/src/db/accounts_repository.h`, `token-backend/src/db/accounts_repository.cpp`
- Create: `token-backend/src/auth/authorizer.h`
- Create: `token-backend/src/auth/static_token_authorizer.h` (header-only)
- Create: `token-backend/test/accounts_repository_tests.cpp`
- Create: `token-backend/test/static_token_authorizer_tests.cpp`
- Modify: `token-backend/CMakeLists.txt`, `token-backend/test/CMakeLists.txt`, `token-backend/src/main.cpp`

**Interfaces:**
- Consumes: `SqliteConnection` (Task 2), `fastHash`/`fastHashMatches` (Task 3).
- Produces:
  ```cpp
  namespace pcm::tokenbackend {
  using AccountId = int64_t;

  class AccountsRepository {
  public:
    explicit AccountsRepository(SqliteConnection &conn);
    // Replaces any existing account row (MVP: exactly one). Returns the plaintext
    // credential — the only time it is ever available in plaintext.
    std::string seedAccount();
    std::optional<AccountId> findByCredential(const std::string &rawCredential);
  private:
    SqliteConnection &conn_;
  };

  class Authorizer {
  public:
    virtual ~Authorizer() = default;
    virtual std::optional<AccountId> authorize(const std::string &bearerCredential) = 0;
  };

  class StaticTokenAuthorizer : public Authorizer {
  public:
    explicit StaticTokenAuthorizer(AccountsRepository &accounts);
    std::optional<AccountId> authorize(const std::string &bearerCredential) override;
  private:
    AccountsRepository &accounts_;
  };
  }
  ```

- [ ] **Step 1: Write the failing repository and authorizer tests**

```cpp
// token-backend/test/accounts_repository_tests.cpp
#include "db/accounts_repository.h"
#include "db/migrations.h"
#include "db/sqlite_connection.h"

#include <gtest/gtest.h>
#include <sodium.h>

class AccountsRepositoryTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(sodium_init() >= 0, true);
    conn = std::make_unique<pcm::tokenbackend::SqliteConnection>(":memory:");
    pcm::tokenbackend::runMigrations(*conn);
  }
  std::unique_ptr<pcm::tokenbackend::SqliteConnection> conn;
};

TEST_F(AccountsRepositoryTest, SeedThenFindByCredentialSucceeds) {
  pcm::tokenbackend::AccountsRepository repo(*conn);
  auto credential = repo.seedAccount();

  auto found = repo.findByCredential(credential);
  ASSERT_TRUE(found.has_value());
}

TEST_F(AccountsRepositoryTest, WrongCredentialReturnsNullopt) {
  pcm::tokenbackend::AccountsRepository repo(*conn);
  repo.seedAccount();

  EXPECT_FALSE(repo.findByCredential("not-the-credential").has_value());
}

TEST_F(AccountsRepositoryTest, ReseedingReplacesThePreviousCredential) {
  pcm::tokenbackend::AccountsRepository repo(*conn);
  auto first = repo.seedAccount();
  auto second = repo.seedAccount();

  EXPECT_FALSE(repo.findByCredential(first).has_value());
  EXPECT_TRUE(repo.findByCredential(second).has_value());
}
```

```cpp
// token-backend/test/static_token_authorizer_tests.cpp
#include "auth/static_token_authorizer.h"
#include "db/accounts_repository.h"
#include "db/migrations.h"
#include "db/sqlite_connection.h"

#include <gtest/gtest.h>
#include <sodium.h>

class StaticTokenAuthorizerTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(sodium_init() >= 0, true);
    conn = std::make_unique<pcm::tokenbackend::SqliteConnection>(":memory:");
    pcm::tokenbackend::runMigrations(*conn);
    accounts = std::make_unique<pcm::tokenbackend::AccountsRepository>(*conn);
    credential = accounts->seedAccount();
  }
  std::unique_ptr<pcm::tokenbackend::SqliteConnection> conn;
  std::unique_ptr<pcm::tokenbackend::AccountsRepository> accounts;
  std::string credential;
};

TEST_F(StaticTokenAuthorizerTest, AuthorizesValidCredential) {
  pcm::tokenbackend::StaticTokenAuthorizer authorizer(*accounts);
  EXPECT_TRUE(authorizer.authorize(credential).has_value());
}

TEST_F(StaticTokenAuthorizerTest, RejectsInvalidCredential) {
  pcm::tokenbackend::StaticTokenAuthorizer authorizer(*accounts);
  EXPECT_FALSE(authorizer.authorize("garbage").has_value());
}
```

- [ ] **Step 2: Add both test files to `test/CMakeLists.txt`**

```cmake
add_executable(token_backend_tests
  health_controller_tests.cpp
  sqlite_connection_tests.cpp
  hashing_tests.cpp
  livekit_jwt_tests.cpp
  accounts_repository_tests.cpp
  static_token_authorizer_tests.cpp
)
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `cmake --build token-backend/build`
Expected: FAIL — `db/accounts_repository.h` and `auth/static_token_authorizer.h` do not exist.

- [ ] **Step 4: Write `src/db/accounts_repository.h`**

```cpp
#pragma once

#include "db/sqlite_connection.h"

#include <cstdint>
#include <optional>
#include <string>

namespace pcm::tokenbackend {

using AccountId = int64_t;

class AccountsRepository {
public:
  explicit AccountsRepository(SqliteConnection &conn) : conn_(conn) {}

  std::string seedAccount();
  std::optional<AccountId> findByCredential(const std::string &rawCredential);

private:
  SqliteConnection &conn_;
};

} // namespace pcm::tokenbackend
```

- [ ] **Step 5: Write `src/db/accounts_repository.cpp`**

```cpp
#include "db/accounts_repository.h"
#include "crypto/hashing.h"
#include "crypto/random_token.h"

#include <chrono>
#include <stdexcept>

namespace pcm::tokenbackend {

namespace {
std::string nowIso8601() {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
  return buf;
}
} // namespace

std::string AccountsRepository::seedAccount() {
  auto credential = generateUrlSafeToken(32);
  auto hash = fastHash(credential);

  conn_.exec("DELETE FROM accounts;");

  sqlite3_stmt *stmt = nullptr;
  const char *sql = "INSERT INTO accounts (credential_hash, created_at) VALUES (?, ?);";
  if (sqlite3_prepare_v2(conn_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare account insert");
  }
  auto createdAt = nowIso8601();
  sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, createdAt.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error("failed to insert account");
  }
  sqlite3_finalize(stmt);

  return credential;
}

std::optional<AccountId> AccountsRepository::findByCredential(const std::string &rawCredential) {
  auto hash = fastHash(rawCredential);

  sqlite3_stmt *stmt = nullptr;
  const char *sql = "SELECT id FROM accounts WHERE credential_hash = ?;";
  if (sqlite3_prepare_v2(conn_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare account lookup");
  }
  sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);

  std::optional<AccountId> result;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    result = sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return result;
}

} // namespace pcm::tokenbackend
```

- [ ] **Step 6: Write `src/auth/authorizer.h`**

```cpp
#pragma once

#include "db/accounts_repository.h"

#include <optional>
#include <string>

namespace pcm::tokenbackend {

class Authorizer {
public:
  virtual ~Authorizer() = default;
  virtual std::optional<AccountId> authorize(const std::string &bearerCredential) = 0;
};

} // namespace pcm::tokenbackend
```

- [ ] **Step 7: Write `src/auth/static_token_authorizer.h` and `.cpp`**

```cpp
// token-backend/src/auth/static_token_authorizer.h
#pragma once

#include "auth/authorizer.h"
#include "db/accounts_repository.h"

namespace pcm::tokenbackend {

class StaticTokenAuthorizer : public Authorizer {
public:
  explicit StaticTokenAuthorizer(AccountsRepository &accounts) : accounts_(accounts) {}
  std::optional<AccountId> authorize(const std::string &bearerCredential) override {
    return accounts_.findByCredential(bearerCredential);
  }

private:
  AccountsRepository &accounts_;
};

} // namespace pcm::tokenbackend
```

(No separate `.cpp` needed — the class is small enough to stay header-only; skip creating an empty translation unit.)

- [ ] **Step 8: Update `CMakeLists.txt`**

```cmake
add_library(token_backend_lib STATIC
  src/controller/health_controller.h
  src/config.cpp
  src/db/sqlite_connection.cpp
  src/db/migrations.cpp
  src/db/accounts_repository.cpp
  src/crypto/random_token.cpp
  src/crypto/hashing.cpp
  src/crypto/livekit_jwt.cpp
  src/auth/authorizer.h
  src/auth/static_token_authorizer.h
)
```

- [ ] **Step 9: Add a `--seed-account` CLI mode to `src/main.cpp`**

Insert near the top of `main()`, before the server starts:

```cpp
#include "auth/static_token_authorizer.h"
#include "config.h"
#include "db/accounts_repository.h"
#include "db/migrations.h"
#include "db/sqlite_connection.h"

// ... inside main(), after oatpp::base::Environment::init():
if (argc > 1 && std::string(argv[1]) == "--seed-account") {
  auto config = pcm::tokenbackend::Config::fromEnv();
  pcm::tokenbackend::SqliteConnection conn(config.dbPath);
  pcm::tokenbackend::runMigrations(conn);
  pcm::tokenbackend::AccountsRepository accounts(conn);
  auto credential = accounts.seedAccount();
  std::cout << "Seeded account. Bearer credential (copy this now, it will not be shown again):\n"
            << credential << std::endl;
  oatpp::base::Environment::destroy();
  return 0;
}
```

- [ ] **Step 10: Build and run tests to verify they pass**

Run: `cmake --build token-backend/build && ctest --test-dir token-backend/build --output-on-failure`
Expected: PASS

- [ ] **Step 11: Commit**

```bash
git add token-backend/
git commit -m "feat(token-backend): add accounts repository, Authorizer seam, and account seeding CLI"
```

---

### Task 6: Meetings and invitations repositories

**Files:**
- Create: `token-backend/src/db/meetings_repository.h`, `token-backend/src/db/meetings_repository.cpp`
- Create: `token-backend/src/db/invitations_repository.h`, `token-backend/src/db/invitations_repository.cpp`
- Create: `token-backend/test/meetings_repository_tests.cpp`
- Create: `token-backend/test/invitations_repository_tests.cpp`
- Modify: `token-backend/CMakeLists.txt`, `token-backend/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `SqliteConnection`, `AccountId` (Task 5), `fastHash`/`fastHashMatches`/`hashPasscode`/`passcodeMatches` (Task 3), `generateUrlSafeToken`/`generateNumericPasscode` (Task 3).
- Produces:
  ```cpp
  namespace pcm::tokenbackend {
  struct Meeting {
    int64_t id;
    std::string meetingRef;
    AccountId accountId;
    std::string roomName;
    std::string scheduledStart; // ISO 8601
    std::string scheduledEnd;   // ISO 8601
    std::string status;         // "active" | "invalidated"
  };

  class MeetingsRepository {
  public:
    explicit MeetingsRepository(SqliteConnection &conn);
    Meeting create(AccountId accountId, const std::string &scheduledStart,
                    const std::string &scheduledEnd);
    std::optional<Meeting> findByRef(const std::string &meetingRef);
    std::optional<Meeting> findById(int64_t meetingId);
    void invalidate(int64_t meetingId);
  private:
    SqliteConnection &conn_;
  };

  struct Invitation {
    int64_t id;
    int64_t meetingId;
    AccountId accountId;
    std::string passcodeHash;
    int passcodeAttempts;
    std::string status;
  };

  class InvitationsRepository {
  public:
    explicit InvitationsRepository(SqliteConnection &conn);
    // Returns the plaintext invitation code and passcode — the only time either
    // is available in plaintext.
    struct CreateResult { std::string invitationCode; std::string passcode; Invitation invitation; };
    CreateResult create(int64_t meetingId, AccountId accountId);
    std::optional<Invitation> findByCode(const std::string &invitationCode);
    // Returns the new attempt count. At 5, the invitation is auto-invalidated.
    int recordFailedPasscodeAttempt(int64_t invitationId);
    void invalidate(int64_t invitationId);
  private:
    SqliteConnection &conn_;
  };
  }
  ```

- [ ] **Step 1: Write the failing repository tests**

```cpp
// token-backend/test/meetings_repository_tests.cpp
#include "db/meetings_repository.h"
#include "db/migrations.h"
#include "db/sqlite_connection.h"

#include <gtest/gtest.h>
#include <sodium.h>

class MeetingsRepositoryTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(sodium_init() >= 0, true);
    conn = std::make_unique<pcm::tokenbackend::SqliteConnection>(":memory:");
    pcm::tokenbackend::runMigrations(*conn);
  }
  std::unique_ptr<pcm::tokenbackend::SqliteConnection> conn;
};

TEST_F(MeetingsRepositoryTest, CreateProducesUniqueRefAndRoomName) {
  pcm::tokenbackend::MeetingsRepository repo(*conn);
  auto a = repo.create(1, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");
  auto b = repo.create(1, "2026-10-02T10:00:00Z", "2026-10-02T10:50:00Z");

  EXPECT_NE(a.meetingRef, b.meetingRef);
  EXPECT_NE(a.roomName, b.roomName);
  EXPECT_EQ(a.status, "active");
}

TEST_F(MeetingsRepositoryTest, FindByRefReturnsTheSameRoomNameEveryTime) {
  pcm::tokenbackend::MeetingsRepository repo(*conn);
  auto created = repo.create(1, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");

  auto first = repo.findByRef(created.meetingRef);
  auto second = repo.findByRef(created.meetingRef);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->roomName, second->roomName);
  EXPECT_EQ(first->roomName, created.roomName);
}

TEST_F(MeetingsRepositoryTest, InvalidateChangesStatus) {
  pcm::tokenbackend::MeetingsRepository repo(*conn);
  auto created = repo.create(1, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");

  repo.invalidate(created.id);

  auto found = repo.findByRef(created.meetingRef);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->status, "invalidated");
}

TEST_F(MeetingsRepositoryTest, FindByUnknownRefReturnsNullopt) {
  pcm::tokenbackend::MeetingsRepository repo(*conn);
  EXPECT_FALSE(repo.findByRef("mtg_does_not_exist").has_value());
}

TEST_F(MeetingsRepositoryTest, FindByIdReturnsSameRowAsFindByRef) {
  pcm::tokenbackend::MeetingsRepository repo(*conn);
  auto created = repo.create(1, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");

  auto byId = repo.findById(created.id);
  ASSERT_TRUE(byId.has_value());
  EXPECT_EQ(byId->meetingRef, created.meetingRef);
  EXPECT_EQ(byId->roomName, created.roomName);
}
```

```cpp
// token-backend/test/invitations_repository_tests.cpp
#include "crypto/hashing.h"
#include "db/invitations_repository.h"
#include "db/meetings_repository.h"
#include "db/migrations.h"
#include "db/sqlite_connection.h"

#include <gtest/gtest.h>
#include <sodium.h>

class InvitationsRepositoryTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(sodium_init() >= 0, true);
    conn = std::make_unique<pcm::tokenbackend::SqliteConnection>(":memory:");
    pcm::tokenbackend::runMigrations(*conn);
    pcm::tokenbackend::MeetingsRepository meetings(*conn);
    meeting = meetings.create(1, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");
  }
  std::unique_ptr<pcm::tokenbackend::SqliteConnection> conn;
  pcm::tokenbackend::Meeting meeting{};
};

TEST_F(InvitationsRepositoryTest, CreateThenFindByCodeSucceeds) {
  pcm::tokenbackend::InvitationsRepository repo(*conn);
  auto created = repo.create(meeting.id, 1);

  auto found = repo.findByCode(created.invitationCode);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->meetingId, meeting.id);
  EXPECT_EQ(found->passcodeAttempts, 0);
  EXPECT_TRUE(pcm::tokenbackend::passcodeMatches(created.passcode, found->passcodeHash));
}

TEST_F(InvitationsRepositoryTest, WrongCodeReturnsNullopt) {
  pcm::tokenbackend::InvitationsRepository repo(*conn);
  repo.create(meeting.id, 1);

  EXPECT_FALSE(repo.findByCode("wrong-code").has_value());
}

TEST_F(InvitationsRepositoryTest, FailedAttemptsAccumulateAndAutoInvalidateAtFive) {
  pcm::tokenbackend::InvitationsRepository repo(*conn);
  auto created = repo.create(meeting.id, 1);

  int attempts = 0;
  for (int i = 0; i < 5; ++i) {
    attempts = repo.recordFailedPasscodeAttempt(created.invitation.id);
  }
  EXPECT_EQ(attempts, 5);

  auto found = repo.findByCode(created.invitationCode);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->status, "invalidated");
}

TEST_F(InvitationsRepositoryTest, ExplicitInvalidateChangesStatus) {
  pcm::tokenbackend::InvitationsRepository repo(*conn);
  auto created = repo.create(meeting.id, 1);

  repo.invalidate(created.invitation.id);

  auto found = repo.findByCode(created.invitationCode);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->status, "invalidated");
}
```

- [ ] **Step 2: Add both files to `test/CMakeLists.txt`**

```cmake
add_executable(token_backend_tests
  health_controller_tests.cpp
  sqlite_connection_tests.cpp
  hashing_tests.cpp
  livekit_jwt_tests.cpp
  accounts_repository_tests.cpp
  static_token_authorizer_tests.cpp
  meetings_repository_tests.cpp
  invitations_repository_tests.cpp
)
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `cmake --build token-backend/build`
Expected: FAIL — `db/meetings_repository.h` and `db/invitations_repository.h` do not exist.

- [ ] **Step 4: Write `src/db/meetings_repository.h`**

```cpp
#pragma once

#include "db/accounts_repository.h"
#include "db/sqlite_connection.h"

#include <optional>
#include <string>

namespace pcm::tokenbackend {

struct Meeting {
  int64_t id;
  std::string meetingRef;
  AccountId accountId;
  std::string roomName;
  std::string scheduledStart;
  std::string scheduledEnd;
  std::string status;
};

class MeetingsRepository {
public:
  explicit MeetingsRepository(SqliteConnection &conn) : conn_(conn) {}

  Meeting create(AccountId accountId, const std::string &scheduledStart,
                  const std::string &scheduledEnd);
  std::optional<Meeting> findByRef(const std::string &meetingRef);
  std::optional<Meeting> findById(int64_t meetingId);
  void invalidate(int64_t meetingId);

private:
  SqliteConnection &conn_;
};

} // namespace pcm::tokenbackend
```

- [ ] **Step 5: Write `src/db/meetings_repository.cpp`**

```cpp
#include "db/meetings_repository.h"
#include "crypto/random_token.h"

#include <chrono>
#include <cstdio>
#include <stdexcept>

namespace pcm::tokenbackend {

namespace {
std::string nowIso8601() {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
  return buf;
}

Meeting readRow(sqlite3_stmt *stmt) {
  Meeting m;
  m.id = sqlite3_column_int64(stmt, 0);
  m.meetingRef = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  m.accountId = sqlite3_column_int64(stmt, 2);
  m.roomName = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
  m.scheduledStart = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
  m.scheduledEnd = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
  m.status = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
  return m;
}

constexpr const char *kSelectColumns =
    "id, meeting_ref, account_id, room_name, scheduled_start, scheduled_end, status";
} // namespace

Meeting MeetingsRepository::create(AccountId accountId, const std::string &scheduledStart,
                                    const std::string &scheduledEnd) {
  std::string meetingRef = "mtg_" + generateUrlSafeToken(9);
  std::string roomName = "rm_" + generateUrlSafeToken(9);
  std::string createdAt = nowIso8601();

  sqlite3_stmt *stmt = nullptr;
  const char *sql = "INSERT INTO meetings "
                     "(meeting_ref, account_id, room_name, scheduled_start, scheduled_end, "
                     "status, created_at) VALUES (?, ?, ?, ?, ?, 'active', ?);";
  if (sqlite3_prepare_v2(conn_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare meeting insert");
  }
  sqlite3_bind_text(stmt, 1, meetingRef.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 2, accountId);
  sqlite3_bind_text(stmt, 3, roomName.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, scheduledStart.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, scheduledEnd.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, createdAt.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error("failed to insert meeting");
  }
  sqlite3_finalize(stmt);

  auto found = findByRef(meetingRef);
  if (!found) {
    throw std::runtime_error("meeting insert succeeded but readback failed");
  }
  return *found;
}

std::optional<Meeting> MeetingsRepository::findByRef(const std::string &meetingRef) {
  sqlite3_stmt *stmt = nullptr;
  std::string sql = std::string("SELECT ") + kSelectColumns + " FROM meetings WHERE meeting_ref = ?;";
  if (sqlite3_prepare_v2(conn_.raw(), sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare meeting lookup");
  }
  sqlite3_bind_text(stmt, 1, meetingRef.c_str(), -1, SQLITE_TRANSIENT);

  std::optional<Meeting> result;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    result = readRow(stmt);
  }
  sqlite3_finalize(stmt);
  return result;
}

std::optional<Meeting> MeetingsRepository::findById(int64_t meetingId) {
  sqlite3_stmt *stmt = nullptr;
  std::string sql = std::string("SELECT ") + kSelectColumns + " FROM meetings WHERE id = ?;";
  if (sqlite3_prepare_v2(conn_.raw(), sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare meeting lookup by id");
  }
  sqlite3_bind_int64(stmt, 1, meetingId);

  std::optional<Meeting> result;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    result = readRow(stmt);
  }
  sqlite3_finalize(stmt);
  return result;
}

void MeetingsRepository::invalidate(int64_t meetingId) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "UPDATE meetings SET status = 'invalidated' WHERE id = ?;";
  if (sqlite3_prepare_v2(conn_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare meeting invalidate");
  }
  sqlite3_bind_int64(stmt, 1, meetingId);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error("failed to invalidate meeting");
  }
  sqlite3_finalize(stmt);
}

} // namespace pcm::tokenbackend
```

- [ ] **Step 6: Write `src/db/invitations_repository.h`**

```cpp
#pragma once

#include "db/accounts_repository.h"
#include "db/sqlite_connection.h"

#include <optional>
#include <string>

namespace pcm::tokenbackend {

struct Invitation {
  int64_t id;
  int64_t meetingId;
  AccountId accountId;
  std::string passcodeHash;
  int passcodeAttempts;
  std::string status;
};

class InvitationsRepository {
public:
  explicit InvitationsRepository(SqliteConnection &conn) : conn_(conn) {}

  struct CreateResult {
    std::string invitationCode;
    std::string passcode;
    Invitation invitation;
  };

  CreateResult create(int64_t meetingId, AccountId accountId);
  std::optional<Invitation> findByCode(const std::string &invitationCode);
  int recordFailedPasscodeAttempt(int64_t invitationId);
  void invalidate(int64_t invitationId);

private:
  SqliteConnection &conn_;
};

} // namespace pcm::tokenbackend
```

- [ ] **Step 7: Write `src/db/invitations_repository.cpp`**

```cpp
#include "db/invitations_repository.h"
#include "crypto/hashing.h"
#include "crypto/random_token.h"

#include <chrono>
#include <stdexcept>

namespace pcm::tokenbackend {

namespace {
std::string nowIso8601() {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
  return buf;
}

constexpr const char *kSelectColumns =
    "id, meeting_id, account_id, passcode_hash, passcode_attempts, status";
constexpr int kMaxPasscodeAttempts = 5;

Invitation readRow(sqlite3_stmt *stmt) {
  Invitation inv;
  inv.id = sqlite3_column_int64(stmt, 0);
  inv.meetingId = sqlite3_column_int64(stmt, 1);
  inv.accountId = sqlite3_column_int64(stmt, 2);
  inv.passcodeHash = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
  inv.passcodeAttempts = sqlite3_column_int(stmt, 4);
  inv.status = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
  return inv;
}
} // namespace

InvitationsRepository::CreateResult InvitationsRepository::create(int64_t meetingId,
                                                                    AccountId accountId) {
  std::string invitationCode = generateUrlSafeToken(24);
  std::string passcode = generateNumericPasscode();
  std::string invitationCodeHash = fastHash(invitationCode);
  std::string passcodeHash = hashPasscode(passcode);
  std::string createdAt = nowIso8601();

  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "INSERT INTO invitations "
      "(meeting_id, account_id, invitation_code_hash, passcode_hash, passcode_attempts, "
      "status, created_at) VALUES (?, ?, ?, ?, 0, 'active', ?);";
  if (sqlite3_prepare_v2(conn_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare invitation insert");
  }
  sqlite3_bind_int64(stmt, 1, meetingId);
  sqlite3_bind_int64(stmt, 2, accountId);
  sqlite3_bind_text(stmt, 3, invitationCodeHash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, passcodeHash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, createdAt.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error("failed to insert invitation");
  }
  sqlite3_finalize(stmt);

  auto found = findByCode(invitationCode);
  if (!found) {
    throw std::runtime_error("invitation insert succeeded but readback failed");
  }
  return CreateResult{invitationCode, passcode, *found};
}

std::optional<Invitation> InvitationsRepository::findByCode(const std::string &invitationCode) {
  auto hash = fastHash(invitationCode);

  sqlite3_stmt *stmt = nullptr;
  std::string sql =
      std::string("SELECT ") + kSelectColumns + " FROM invitations WHERE invitation_code_hash = ?;";
  if (sqlite3_prepare_v2(conn_.raw(), sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare invitation lookup");
  }
  sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);

  std::optional<Invitation> result;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    result = readRow(stmt);
  }
  sqlite3_finalize(stmt);
  return result;
}

int InvitationsRepository::recordFailedPasscodeAttempt(int64_t invitationId) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "UPDATE invitations SET passcode_attempts = passcode_attempts + 1 "
                     "WHERE id = ?;";
  if (sqlite3_prepare_v2(conn_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare attempt increment");
  }
  sqlite3_bind_int64(stmt, 1, invitationId);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error("failed to increment passcode attempts");
  }
  sqlite3_finalize(stmt);

  sqlite3_stmt *readStmt = nullptr;
  sqlite3_prepare_v2(conn_.raw(), "SELECT passcode_attempts FROM invitations WHERE id = ?;", -1,
                      &readStmt, nullptr);
  sqlite3_bind_int64(readStmt, 1, invitationId);
  int attempts = 0;
  if (sqlite3_step(readStmt) == SQLITE_ROW) {
    attempts = sqlite3_column_int(readStmt, 0);
  }
  sqlite3_finalize(readStmt);

  if (attempts >= kMaxPasscodeAttempts) {
    invalidate(invitationId);
  }
  return attempts;
}

void InvitationsRepository::invalidate(int64_t invitationId) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "UPDATE invitations SET status = 'invalidated' WHERE id = ?;";
  if (sqlite3_prepare_v2(conn_.raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare invitation invalidate");
  }
  sqlite3_bind_int64(stmt, 1, invitationId);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error("failed to invalidate invitation");
  }
  sqlite3_finalize(stmt);
}

} // namespace pcm::tokenbackend
```

- [ ] **Step 8: Update `CMakeLists.txt`**

```cmake
add_library(token_backend_lib STATIC
  src/controller/health_controller.h
  src/config.cpp
  src/db/sqlite_connection.cpp
  src/db/migrations.cpp
  src/db/accounts_repository.cpp
  src/db/meetings_repository.cpp
  src/db/invitations_repository.cpp
  src/crypto/random_token.cpp
  src/crypto/hashing.cpp
  src/crypto/livekit_jwt.cpp
  src/auth/authorizer.h
  src/auth/static_token_authorizer.h
)
```

- [ ] **Step 9: Build and run tests to verify they pass**

Run: `cmake --build token-backend/build && ctest --test-dir token-backend/build --output-on-failure`
Expected: PASS

- [ ] **Step 10: Commit**

```bash
git add token-backend/
git commit -m "feat(token-backend): add meetings and invitations repositories with passcode rate limiting"
```

---

### Task 7: MeetingService — the business logic

**Files:**
- Create: `token-backend/src/service/meeting_service.h`, `token-backend/src/service/meeting_service.cpp`
- Create: `token-backend/test/meeting_service_tests.cpp`
- Modify: `token-backend/CMakeLists.txt`, `token-backend/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `Authorizer` (Task 5), `MeetingsRepository`/`InvitationsRepository` (Task 6), `mintLiveKitJwt`/`VideoGrants` (Task 4), `Config` (Task 2).
- Produces:
  ```cpp
  namespace pcm::tokenbackend {
  struct TokenResult {
    std::string endpointUrl; // e.g. "ws://46.173.25.218:7880"
    std::string roomName;
    std::string jwt;
    int64_t expiresAtUnix;
  };

  enum class ServiceError {
    Unauthorized,
    NotFound,
    WrongPasscode,
    TooManyAttempts,
    MeetingWindowClosed,
  };

  template <typename T>
  struct Result {
    std::optional<T> value;
    std::optional<ServiceError> error;
    bool ok() const { return value.has_value(); }
  };

  class MeetingService {
  public:
    MeetingService(Authorizer &authorizer, MeetingsRepository &meetings,
                    InvitationsRepository &invitations, const Config &config,
                    std::string liveKitEndpointUrl);

    struct CreateMeetingOutcome {
      std::string meetingRef;
      std::string invitationCode;
      std::string passcode;
      std::string scheduledStart;
      std::string scheduledEnd;
    };
    Result<CreateMeetingOutcome> createMeeting(const std::string &bearerCredential,
                                                const std::string &scheduledStart,
                                                const std::string &scheduledEnd);

    Result<TokenResult> issueSpecialistToken(const std::string &bearerCredential,
                                              const std::string &meetingRef);

    Result<TokenResult> issueClientToken(const std::string &invitationCode,
                                          const std::string &passcode);

    Result<std::monostate> invalidateMeeting(const std::string &bearerCredential,
                                              const std::string &meetingRef);

  private:
    Authorizer &authorizer_;
    MeetingsRepository &meetings_;
    InvitationsRepository &invitations_;
    const Config &config_;
    std::string liveKitEndpointUrl_;
  };
  }
  ```

- [ ] **Step 1: Write the failing service tests**

```cpp
// token-backend/test/meeting_service_tests.cpp
#include "service/meeting_service.h"

#include "auth/static_token_authorizer.h"
#include "config.h"
#include "db/accounts_repository.h"
#include "db/invitations_repository.h"
#include "db/meetings_repository.h"
#include "db/migrations.h"
#include "db/sqlite_connection.h"

#include <gtest/gtest.h>
#include <sodium.h>

class MeetingServiceTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(sodium_init() >= 0, true);
    conn = std::make_unique<pcm::tokenbackend::SqliteConnection>(":memory:");
    pcm::tokenbackend::runMigrations(*conn);

    accounts = std::make_unique<pcm::tokenbackend::AccountsRepository>(*conn);
    credential = accounts->seedAccount();
    authorizer = std::make_unique<pcm::tokenbackend::StaticTokenAuthorizer>(*accounts);
    meetings = std::make_unique<pcm::tokenbackend::MeetingsRepository>(*conn);
    invitations = std::make_unique<pcm::tokenbackend::InvitationsRepository>(*conn);

    config.liveKitApiKey = "test-key";
    config.liveKitApiSecret = "test-secret";
    config.tokenTtlSeconds = 600;

    service = std::make_unique<pcm::tokenbackend::MeetingService>(
        *authorizer, *meetings, *invitations, config, "ws://livekit.test:7880");
  }

  std::unique_ptr<pcm::tokenbackend::SqliteConnection> conn;
  std::unique_ptr<pcm::tokenbackend::AccountsRepository> accounts;
  std::unique_ptr<pcm::tokenbackend::Authorizer> authorizer;
  std::unique_ptr<pcm::tokenbackend::MeetingsRepository> meetings;
  std::unique_ptr<pcm::tokenbackend::InvitationsRepository> invitations;
  std::unique_ptr<pcm::tokenbackend::MeetingService> service;
  pcm::tokenbackend::Config config{};
  std::string credential;
};

TEST_F(MeetingServiceTest, CreateMeetingRejectsBadCredential) {
  auto result = service->createMeeting("wrong-credential", "2026-10-01T10:00:00Z",
                                        "2026-10-01T10:50:00Z");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.error, pcm::tokenbackend::ServiceError::Unauthorized);
}

TEST_F(MeetingServiceTest, CreateMeetingSucceedsWithGoodCredential) {
  auto result =
      service->createMeeting(credential, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result.value->meetingRef.empty());
  EXPECT_FALSE(result.value->invitationCode.empty());
  EXPECT_EQ(result.value->passcode.size(), 6u);
}

TEST_F(MeetingServiceTest, SpecialistTokenReturnsSameRoomAsCreatedMeeting) {
  auto created =
      service->createMeeting(credential, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");
  ASSERT_TRUE(created.ok());

  auto token = service->issueSpecialistToken(credential, created.value->meetingRef);
  ASSERT_TRUE(token.ok());
  EXPECT_FALSE(token.value->jwt.empty());
  EXPECT_EQ(token.value->endpointUrl, "ws://livekit.test:7880");
}

TEST_F(MeetingServiceTest, SpecialistTokenRejectsBadCredential) {
  auto created =
      service->createMeeting(credential, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");
  ASSERT_TRUE(created.ok());

  auto token = service->issueSpecialistToken("wrong", created.value->meetingRef);
  EXPECT_FALSE(token.ok());
  EXPECT_EQ(token.error, pcm::tokenbackend::ServiceError::Unauthorized);
}

TEST_F(MeetingServiceTest, ClientTokenSucceedsWithCorrectCodeAndPasscode) {
  auto created =
      service->createMeeting(credential, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");
  ASSERT_TRUE(created.ok());

  auto token =
      service->issueClientToken(created.value->invitationCode, created.value->passcode);
  ASSERT_TRUE(token.ok());
  EXPECT_FALSE(token.value->jwt.empty());
}

TEST_F(MeetingServiceTest, ClientTokenRejectsWrongPasscodeAndCountsAttempt) {
  auto created =
      service->createMeeting(credential, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");
  ASSERT_TRUE(created.ok());

  auto result = service->issueClientToken(created.value->invitationCode, "000000");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.error, pcm::tokenbackend::ServiceError::WrongPasscode);
}

TEST_F(MeetingServiceTest, ClientTokenLocksOutAfterFiveWrongPasscodes) {
  auto created =
      service->createMeeting(credential, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");
  ASSERT_TRUE(created.ok());

  for (int i = 0; i < 5; ++i) {
    service->issueClientToken(created.value->invitationCode, "000000");
  }

  auto result =
      service->issueClientToken(created.value->invitationCode, created.value->passcode);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.error, pcm::tokenbackend::ServiceError::TooManyAttempts);
}

TEST_F(MeetingServiceTest, ClientAndSpecialistTokensShareTheSameRoom) {
  auto created =
      service->createMeeting(credential, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");
  ASSERT_TRUE(created.ok());

  auto specialistToken = service->issueSpecialistToken(credential, created.value->meetingRef);
  auto clientToken =
      service->issueClientToken(created.value->invitationCode, created.value->passcode);
  ASSERT_TRUE(specialistToken.ok());
  ASSERT_TRUE(clientToken.ok());
  EXPECT_EQ(specialistToken.value->roomName, clientToken.value->roomName);
}

TEST_F(MeetingServiceTest, InvalidateStopsFurtherTokenIssuance) {
  auto created =
      service->createMeeting(credential, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");
  ASSERT_TRUE(created.ok());

  auto invalidateResult = service->invalidateMeeting(credential, created.value->meetingRef);
  EXPECT_TRUE(invalidateResult.ok());

  auto token = service->issueSpecialistToken(credential, created.value->meetingRef);
  EXPECT_FALSE(token.ok());
  EXPECT_EQ(token.error, pcm::tokenbackend::ServiceError::MeetingWindowClosed);

  auto clientToken =
      service->issueClientToken(created.value->invitationCode, created.value->passcode);
  EXPECT_FALSE(clientToken.ok());
}

TEST_F(MeetingServiceTest, ClientCanReconnectAfterFirstSuccessfulJoin) {
  auto created =
      service->createMeeting(credential, "2026-10-01T10:00:00Z", "2026-10-01T10:50:00Z");
  ASSERT_TRUE(created.ok());

  auto firstJoin =
      service->issueClientToken(created.value->invitationCode, created.value->passcode);
  auto secondJoin =
      service->issueClientToken(created.value->invitationCode, created.value->passcode);

  ASSERT_TRUE(firstJoin.ok());
  ASSERT_TRUE(secondJoin.ok())
      << "invitation code must stay valid for reconnects, not be single-use";
}
```

- [ ] **Step 2: Add the test file to `test/CMakeLists.txt`**

```cmake
add_executable(token_backend_tests
  health_controller_tests.cpp
  sqlite_connection_tests.cpp
  hashing_tests.cpp
  livekit_jwt_tests.cpp
  accounts_repository_tests.cpp
  static_token_authorizer_tests.cpp
  meetings_repository_tests.cpp
  invitations_repository_tests.cpp
  meeting_service_tests.cpp
)
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `cmake --build token-backend/build`
Expected: FAIL — `service/meeting_service.h` does not exist.

- [ ] **Step 4: Write `src/service/meeting_service.h`**

```cpp
#pragma once

#include "auth/authorizer.h"
#include "config.h"
#include "crypto/livekit_jwt.h"
#include "db/invitations_repository.h"
#include "db/meetings_repository.h"

#include <optional>
#include <string>
#include <variant>

namespace pcm::tokenbackend {

struct TokenResult {
  std::string endpointUrl;
  std::string roomName;
  std::string jwt;
  int64_t expiresAtUnix;
};

enum class ServiceError {
  Unauthorized,
  NotFound,
  WrongPasscode,
  TooManyAttempts,
  MeetingWindowClosed,
};

template <typename T> struct Result {
  std::optional<T> value;
  std::optional<ServiceError> error;
  bool ok() const { return value.has_value(); }
};

class MeetingService {
public:
  MeetingService(Authorizer &authorizer, MeetingsRepository &meetings,
                  InvitationsRepository &invitations, const Config &config,
                  std::string liveKitEndpointUrl)
      : authorizer_(authorizer), meetings_(meetings), invitations_(invitations),
        config_(config), liveKitEndpointUrl_(std::move(liveKitEndpointUrl)) {}

  struct CreateMeetingOutcome {
    std::string meetingRef;
    std::string invitationCode;
    std::string passcode;
    std::string scheduledStart;
    std::string scheduledEnd;
  };
  Result<CreateMeetingOutcome> createMeeting(const std::string &bearerCredential,
                                              const std::string &scheduledStart,
                                              const std::string &scheduledEnd);

  Result<TokenResult> issueSpecialistToken(const std::string &bearerCredential,
                                            const std::string &meetingRef);

  Result<TokenResult> issueClientToken(const std::string &invitationCode,
                                        const std::string &passcode);

  Result<std::monostate> invalidateMeeting(const std::string &bearerCredential,
                                            const std::string &meetingRef);

private:
  Authorizer &authorizer_;
  MeetingsRepository &meetings_;
  InvitationsRepository &invitations_;
  const Config &config_;
  std::string liveKitEndpointUrl_;
};

} // namespace pcm::tokenbackend
```

- [ ] **Step 5: Write `src/service/meeting_service.cpp`**

```cpp
#include "service/meeting_service.h"

#include <chrono>

namespace pcm::tokenbackend {

Result<MeetingService::CreateMeetingOutcome>
MeetingService::createMeeting(const std::string &bearerCredential,
                               const std::string &scheduledStart,
                               const std::string &scheduledEnd) {
  auto accountId = authorizer_.authorize(bearerCredential);
  if (!accountId) {
    return {std::nullopt, ServiceError::Unauthorized};
  }

  auto meeting = meetings_.create(*accountId, scheduledStart, scheduledEnd);
  auto invitation = invitations_.create(meeting.id, *accountId);

  CreateMeetingOutcome outcome;
  outcome.meetingRef = meeting.meetingRef;
  outcome.invitationCode = invitation.invitationCode;
  outcome.passcode = invitation.passcode;
  outcome.scheduledStart = meeting.scheduledStart;
  outcome.scheduledEnd = meeting.scheduledEnd;
  return {outcome, std::nullopt};
}

Result<TokenResult> MeetingService::issueSpecialistToken(const std::string &bearerCredential,
                                                           const std::string &meetingRef) {
  auto accountId = authorizer_.authorize(bearerCredential);
  if (!accountId) {
    return {std::nullopt, ServiceError::Unauthorized};
  }

  auto meeting = meetings_.findByRef(meetingRef);
  if (!meeting || meeting->accountId != *accountId) {
    return {std::nullopt, ServiceError::NotFound};
  }
  if (meeting->status != "active") {
    return {std::nullopt, ServiceError::MeetingWindowClosed};
  }

  VideoGrants grants;
  grants.room = meeting->roomName;
  std::string identity = "practitioner-" + meeting->meetingRef;
  auto jwt = mintLiveKitJwt(config_.liveKitApiKey, config_.liveKitApiSecret, identity, grants,
                             config_.tokenTtlSeconds);

  auto now = std::chrono::system_clock::now();
  auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

  TokenResult result;
  result.endpointUrl = liveKitEndpointUrl_;
  result.roomName = meeting->roomName;
  result.jwt = jwt;
  result.expiresAtUnix = nowSeconds + config_.tokenTtlSeconds;
  return {result, std::nullopt};
}

Result<TokenResult> MeetingService::issueClientToken(const std::string &invitationCode,
                                                       const std::string &passcode) {
  auto invitation = invitations_.findByCode(invitationCode);
  if (!invitation) {
    return {std::nullopt, ServiceError::NotFound};
  }
  if (invitation->status != "active") {
    return {std::nullopt, ServiceError::MeetingWindowClosed};
  }

  if (!passcodeMatches(passcode, invitation->passcodeHash)) {
    int attempts = invitations_.recordFailedPasscodeAttempt(invitation->id);
    if (attempts >= 5) {
      return {std::nullopt, ServiceError::TooManyAttempts};
    }
    return {std::nullopt, ServiceError::WrongPasscode};
  }

  auto meeting = meetings_.findById(invitation->meetingId);
  if (!meeting || meeting->status != "active") {
    return {std::nullopt, ServiceError::MeetingWindowClosed};
  }

  VideoGrants grants;
  grants.room = meeting->roomName;
  std::string identity = "client-" + meeting->meetingRef;
  auto jwt = mintLiveKitJwt(config_.liveKitApiKey, config_.liveKitApiSecret, identity, grants,
                             config_.tokenTtlSeconds);

  auto now = std::chrono::system_clock::now();
  auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

  TokenResult result;
  result.endpointUrl = liveKitEndpointUrl_;
  result.roomName = meeting->roomName;
  result.jwt = jwt;
  result.expiresAtUnix = nowSeconds + config_.tokenTtlSeconds;
  return {result, std::nullopt};
}

Result<std::monostate> MeetingService::invalidateMeeting(const std::string &bearerCredential,
                                                           const std::string &meetingRef) {
  auto accountId = authorizer_.authorize(bearerCredential);
  if (!accountId) {
    return {std::nullopt, ServiceError::Unauthorized};
  }

  auto meeting = meetings_.findByRef(meetingRef);
  if (!meeting || meeting->accountId != *accountId) {
    return {std::nullopt, ServiceError::NotFound};
  }

  meetings_.invalidate(meeting->id);
  return {std::monostate{}, std::nullopt};
}

} // namespace pcm::tokenbackend
```

Note: `issueClientToken` resolves the meeting via `MeetingsRepository::findById`
(Task 6) using `invitation->meetingId` — the invitation only carries the
meeting's internal integer id, not its `meeting_ref` string, so `findByRef`
does not apply here.

- [ ] **Step 6: Update `CMakeLists.txt`**

```cmake
add_library(token_backend_lib STATIC
  src/controller/health_controller.h
  src/config.cpp
  src/db/sqlite_connection.cpp
  src/db/migrations.cpp
  src/db/accounts_repository.cpp
  src/db/meetings_repository.cpp
  src/db/invitations_repository.cpp
  src/crypto/random_token.cpp
  src/crypto/hashing.cpp
  src/crypto/livekit_jwt.cpp
  src/auth/authorizer.h
  src/auth/static_token_authorizer.h
  src/service/meeting_service.cpp
)
```

- [ ] **Step 7: Build and run tests to verify they pass**

Run: `cmake --build token-backend/build && ctest --test-dir token-backend/build --output-on-failure`
Expected: PASS — including `ClientAndSpecialistTokensShareTheSameRoom` and
`ClientCanReconnectAfterFirstSuccessfulJoin`, which directly verify the ADR-12
guarantees.

- [ ] **Step 8: Commit**

```bash
git add token-backend/
git commit -m "feat(token-backend): add MeetingService business logic wiring auth, repositories, and JWT minting"
```

---

### Task 8: HTTP controllers

**Files:**
- Create: `token-backend/src/controller/dto.h`
- Create: `token-backend/src/controller/meetings_controller.h`
- Create: `token-backend/src/controller/invitations_controller.h`
- Modify: `token-backend/src/main.cpp`
- Modify: `token-backend/CMakeLists.txt`

**Interfaces:**
- Consumes: `MeetingService`, `Result<T>`, `ServiceError` (Task 7).
- Produces: two `oatpp::web::server::api::ApiController` subclasses wired into `main.cpp`'s router. No new testable pure-C++ interfaces — controller logic is intentionally thin and delegates to the already-tested `MeetingService`.

- [ ] **Step 1: Write `src/controller/dto.h`**

```cpp
#pragma once

#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"

namespace pcm::tokenbackend {

#include OATPP_CODEGEN_BEGIN(DTO)

class CreateMeetingRequestDto : public oatpp::DTO {
  DTO_INIT(CreateMeetingRequestDto, DTO)
  DTO_FIELD(String, scheduledStart);
  DTO_FIELD(String, scheduledEnd);
};

class CreateMeetingResponseDto : public oatpp::DTO {
  DTO_INIT(CreateMeetingResponseDto, DTO)
  DTO_FIELD(String, meetingRef);
  DTO_FIELD(String, invitationUrl);
  DTO_FIELD(String, passcode);
  DTO_FIELD(String, scheduledStart);
  DTO_FIELD(String, scheduledEnd);
};

class TokenResponseDto : public oatpp::DTO {
  DTO_INIT(TokenResponseDto, DTO)
  DTO_FIELD(String, endpointUrl);
  DTO_FIELD(String, roomName);
  DTO_FIELD(String, token);
  DTO_FIELD(Int64, expiresAt);
};

class ClientTokenRequestDto : public oatpp::DTO {
  DTO_INIT(ClientTokenRequestDto, DTO)
  DTO_FIELD(String, passcode);
};

class ErrorResponseDto : public oatpp::DTO {
  DTO_INIT(ErrorResponseDto, DTO)
  DTO_FIELD(String, error);
};

#include OATPP_CODEGEN_END(DTO)

} // namespace pcm::tokenbackend
```

- [ ] **Step 2: Write `src/controller/meetings_controller.h`**

```cpp
#pragma once

#include "controller/dto.h"
#include "service/meeting_service.h"

#include "oatpp/web/server/api/ApiController.hpp"
#include "oatpp/core/macro/codegen.hpp"

namespace pcm::tokenbackend {

namespace {
inline oatpp::web::protocol::http::Status statusForError(ServiceError err) {
  switch (err) {
  case ServiceError::Unauthorized:
    return oatpp::web::protocol::http::Status::CODE_401;
  case ServiceError::NotFound:
    return oatpp::web::protocol::http::Status::CODE_404;
  case ServiceError::WrongPasscode:
    return oatpp::web::protocol::http::Status::CODE_401;
  case ServiceError::TooManyAttempts:
    return oatpp::web::protocol::http::Status::CODE_429;
  case ServiceError::MeetingWindowClosed:
    return oatpp::web::protocol::http::Status::CODE_410;
  }
  return oatpp::web::protocol::http::Status::CODE_500;
}
} // namespace

#include OATPP_CODEGEN_BEGIN(ApiController)

class MeetingsController : public oatpp::web::server::api::ApiController {
public:
  MeetingsController(const std::shared_ptr<ObjectMapper> &objectMapper,
                      MeetingService &service, const std::string &invitationBaseUrl)
      : oatpp::web::server::api::ApiController(objectMapper), service_(service),
        invitationBaseUrl_(invitationBaseUrl) {}

  ENDPOINT("POST", "/v1/meetings", createMeeting, HEADER(String, authHeader, "Authorization"),
            BODY_DTO(Object<CreateMeetingRequestDto>, body)) {
    auto result = service_.createMeeting(authHeader ? authHeader->std_str() : "",
                                          body->scheduledStart->std_str(),
                                          body->scheduledEnd->std_str());
    if (!result.ok()) {
      auto err = ErrorResponseDto::createShared();
      err->error = "unauthorized";
      return createDtoResponse(statusForError(*result.error), err);
    }
    auto dto = CreateMeetingResponseDto::createShared();
    dto->meetingRef = result.value->meetingRef;
    dto->invitationUrl = invitationBaseUrl_ + result.value->invitationCode;
    dto->passcode = result.value->passcode;
    dto->scheduledStart = result.value->scheduledStart;
    dto->scheduledEnd = result.value->scheduledEnd;
    return createDtoResponse(Status::CODE_200, dto);
  }

  ENDPOINT("POST", "/v1/meetings/{meetingRef}/specialist-token", specialistToken,
            PATH(String, meetingRef), HEADER(String, authHeader, "Authorization")) {
    auto result =
        service_.issueSpecialistToken(authHeader ? authHeader->std_str() : "", meetingRef->std_str());
    if (!result.ok()) {
      auto err = ErrorResponseDto::createShared();
      err->error = "request_failed";
      return createDtoResponse(statusForError(*result.error), err);
    }
    auto dto = TokenResponseDto::createShared();
    dto->endpointUrl = result.value->endpointUrl;
    dto->roomName = result.value->roomName;
    dto->token = result.value->jwt;
    dto->expiresAt = result.value->expiresAtUnix;
    return createDtoResponse(Status::CODE_200, dto);
  }

  ENDPOINT("POST", "/v1/meetings/{meetingRef}/invalidate", invalidateMeeting,
            PATH(String, meetingRef), HEADER(String, authHeader, "Authorization")) {
    auto result =
        service_.invalidateMeeting(authHeader ? authHeader->std_str() : "", meetingRef->std_str());
    if (!result.ok()) {
      auto err = ErrorResponseDto::createShared();
      err->error = "request_failed";
      return createDtoResponse(statusForError(*result.error), err);
    }
    return createResponse(Status::CODE_204, "");
  }

private:
  MeetingService &service_;
  std::string invitationBaseUrl_;
};

#include OATPP_CODEGEN_END(ApiController)

} // namespace pcm::tokenbackend
```

- [ ] **Step 3: Write `src/controller/invitations_controller.h`**

```cpp
#pragma once

#include "controller/dto.h"
#include "controller/meetings_controller.h" // for statusForError
#include "service/meeting_service.h"

#include "oatpp/web/server/api/ApiController.hpp"
#include "oatpp/core/macro/codegen.hpp"

namespace pcm::tokenbackend {

#include OATPP_CODEGEN_BEGIN(ApiController)

class InvitationsController : public oatpp::web::server::api::ApiController {
public:
  InvitationsController(const std::shared_ptr<ObjectMapper> &objectMapper,
                         MeetingService &service)
      : oatpp::web::server::api::ApiController(objectMapper), service_(service) {}

  ENDPOINT("POST", "/v1/invitations/{code}/client-token", clientToken, PATH(String, code),
            BODY_DTO(Object<ClientTokenRequestDto>, body)) {
    auto result = service_.issueClientToken(code->std_str(),
                                             body->passcode ? body->passcode->std_str() : "");
    if (!result.ok()) {
      auto err = ErrorResponseDto::createShared();
      err->error = "request_failed";
      return createDtoResponse(statusForError(*result.error), err);
    }
    auto dto = TokenResponseDto::createShared();
    dto->endpointUrl = result.value->endpointUrl;
    dto->roomName = result.value->roomName;
    dto->token = result.value->jwt;
    dto->expiresAt = result.value->expiresAtUnix;
    return createDtoResponse(Status::CODE_200, dto);
  }

private:
  MeetingService &service_;
};

#include OATPP_CODEGEN_END(ApiController)

} // namespace pcm::tokenbackend
```

- [ ] **Step 4: Wire both controllers into `src/main.cpp`**

Replace the `HealthController` wiring block with:

```cpp
#include "controller/health_controller.h"
#include "controller/meetings_controller.h"
#include "controller/invitations_controller.h"
#include "service/meeting_service.h"
#include "auth/static_token_authorizer.h"
#include "db/accounts_repository.h"
#include "db/invitations_repository.h"
#include "db/meetings_repository.h"
#include "db/migrations.h"
#include "db/sqlite_connection.h"
#include "config.h"

// ... inside main(), replacing the previous router/controller block:
auto config = pcm::tokenbackend::Config::fromEnv();
pcm::tokenbackend::SqliteConnection conn(config.dbPath);
pcm::tokenbackend::runMigrations(conn);

pcm::tokenbackend::AccountsRepository accounts(conn);
pcm::tokenbackend::StaticTokenAuthorizer authorizer(accounts);
pcm::tokenbackend::MeetingsRepository meetings(conn);
pcm::tokenbackend::InvitationsRepository invitations(conn);

const char *endpointEnv = std::getenv("LIVEKIT_WS_ENDPOINT");
std::string liveKitEndpoint = endpointEnv ? endpointEnv : "ws://46.173.25.218:7880";
const char *invitationBaseEnv = std::getenv("INVITATION_BASE_URL");
std::string invitationBase =
    invitationBaseEnv ? invitationBaseEnv : "https://example.invalid/join/";

pcm::tokenbackend::MeetingService service(authorizer, meetings, invitations, config,
                                           liveKitEndpoint);

auto router = oatpp::web::server::HttpRouter::createShared();

pcm::tokenbackend::HealthController healthController;
healthController.addEndpointsToRouter(router);

pcm::tokenbackend::MeetingsController meetingsController(mappers->getDefaultMapper(), service,
                                                           invitationBase);
meetingsController.addEndpointsToRouter(router);

pcm::tokenbackend::InvitationsController invitationsController(mappers->getDefaultMapper(),
                                                                  service);
invitationsController.addEndpointsToRouter(router);
```

- [ ] **Step 5: Build to verify it compiles**

Run: `cmake --build token-backend/build`
Expected: builds successfully. Fix any DTO/macro syntax mismatches oat++'s
compiler errors point at — the business logic under test is already covered
by Task 7's `meeting_service_tests`; this step only needs the HTTP wiring to
compile.

- [ ] **Step 6: Manual smoke test against a running instance**

Run:
```bash
LIVEKIT_API_KEY=test LIVEKIT_API_SECRET=test DB_PATH=/tmp/tb.sqlite3 \
  token-backend/build/pcm_token_backend --seed-account
```
Copy the printed credential, then in a second terminal:
```bash
LIVEKIT_API_KEY=test LIVEKIT_API_SECRET=test DB_PATH=/tmp/tb.sqlite3 \
  token-backend/build/pcm_token_backend &
curl -X POST localhost:8080/v1/meetings \
  -H "Authorization: <credential>" -H "Content-Type: application/json" \
  -d '{"scheduledStart":"2026-10-01T10:00:00Z","scheduledEnd":"2026-10-01T10:50:00Z"}'
```
Expected: HTTP 200 with `meetingRef`, `invitationUrl`, and a 6-digit `passcode`.

- [ ] **Step 7: Commit**

```bash
git add token-backend/
git commit -m "feat(token-backend): wire HTTP controllers for meetings and invitations endpoints"
```

---

### Task 9: Deployment — Dockerfile and PSY server integration

**Files:**
- Create: `token-backend/Dockerfile`
- Create: `token-backend/README.md`
- Create: `token-backend/scripts/deploy-note.md`

**Interfaces:** None — this task only affects deployment artifacts, not application code.

- [ ] **Step 1: Write `token-backend/Dockerfile`**

```dockerfile
FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ninja-build curl zip unzip tar pkg-config \
    && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 https://github.com/microsoft/vcpkg /opt/vcpkg \
    && /opt/vcpkg/bootstrap-vcpkg.sh
ENV VCPKG_ROOT=/opt/vcpkg
WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DCMAKE_BUILD_TYPE=Release -DPCM_TB_BUILD_TESTS=OFF \
    && cmake --build build

FROM ubuntu:24.04 AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/pcm_token_backend /usr/local/bin/pcm_token_backend
ENV DB_PATH=/data/token-backend.sqlite3
VOLUME ["/data"]
EXPOSE 8080
ENTRYPOINT ["/usr/local/bin/pcm_token_backend"]
```

- [ ] **Step 2: Write `token-backend/README.md`**

```markdown
# PsyClientManager token backend

Mints short-lived, room-scoped LiveKit JWTs for the self-hosted LiveKit
deployment. Never holds anything the desktop app needs to trust beyond a
signed token — the LiveKit API secret and the bearer credential live only
here. See `docs/asciidoc/11-token-backend-account-model-adr.adoc` and
`docs/asciidoc/12-invitation-security-model-adr.adoc` for the design this
implements.

## Build

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build
ctest --test-dir build --output-on-failure
```

## Configuration (environment variables)

| Variable | Required | Default | Purpose |
|---|---|---|---|
| `PORT` | no | `8080` | HTTP listen port |
| `DB_PATH` | no | `token-backend.sqlite3` | SQLite database file |
| `LIVEKIT_API_KEY` | yes | — | Must match the self-hosted LiveKit server's key |
| `LIVEKIT_API_SECRET` | yes | — | Must match the self-hosted LiveKit server's secret |
| `LIVEKIT_WS_ENDPOINT` | no | `ws://46.173.25.218:7880` | Returned to clients as the connection URL |
| `INVITATION_BASE_URL` | no | `https://example.invalid/join/` | Prefix for invitation links; set for real once a domain exists |
| `TOKEN_TTL_SECONDS` | no | `600` | LiveKit JWT lifetime |

## First deploy: seed the one account

```bash
pcm_token_backend --seed-account
```

Copy the printed bearer credential into PsyClientManager's Settings once —
it is never shown again. Losing it means re-seeding, which invalidates the
previous credential (see `AccountsRepository::seedAccount`).
```

- [ ] **Step 3: Write `token-backend/scripts/deploy-note.md`**

```markdown
# Deploying alongside the existing LiveKit containers

This follows the same `/opt/livekit/docker-compose.yml` pattern already
running on the PSY server for `livekit` and `meet`. Add a sibling service —
do not put this inside the `livekit` container.

```yaml
  token-backend:
    build:
      context: /opt/pcm-token-backend
      dockerfile: Dockerfile
    container_name: pcm-token-backend
    network_mode: host
    restart: unless-stopped
    environment:
      LIVEKIT_API_KEY: <same value as the livekit service>
      LIVEKIT_API_SECRET: <same value as the livekit service>
      LIVEKIT_WS_ENDPOINT: ws://46.173.25.218:7880
      DB_PATH: /data/token-backend.sqlite3
    volumes:
      - /opt/pcm-token-backend/data:/data
```

Copy `token-backend/` from this repo to `/opt/pcm-token-backend` on the PSY
server (same layout used for the LiveKit Meet build earlier), then
`podman-compose build token-backend && podman-compose up -d token-backend`.
Open `8080/tcp` in firewalld the same way `3000/tcp` was opened for Meet.

TLS is intentionally not part of this: it stays behind Caddy once a domain
exists, exactly like the deferred `wss://` work for LiveKit itself.
```

- [ ] **Step 4: Commit**

```bash
git add token-backend/
git commit -m "docs(token-backend): add Dockerfile, README, and PSY-server deployment notes"
```

---

## Self-Review Notes

- **Spec coverage:** all four `docs/video-roadmap.md` §5.4 endpoints are implemented (Task 8); the account/`Authorizer` seam from ADR-11 is Task 5; the reusable-invitation, passcode, and fixed-identity guarantees from ADR-12 are enforced in `MeetingService` and directly asserted by `ClientCanReconnectAfterFirstSuccessfulJoin` and `ClientAndSpecialistTokensShareTheSameRoom` (Task 7). Not covered by this plan, by design: TLS (deferred), desktop Settings UI for pasting the credential (belongs to issue #80), and the scheduled-window-based expiry of passcodes beyond "meeting invalidated" (the repository stores `scheduled_start`/`scheduled_end`; wiring a background sweep or an on-read check against "now" is a small follow-up once real appointment data flows in from issue #79 — flag this explicitly rather than silently claim it's done).
- **Type consistency:** `Result<T>` and `ServiceError` (Task 7) are used identically by both controllers in Task 8; `MeetingsRepository::findById` is defined in Task 6 (header, `.cpp`, and a dedicated test) alongside `findByRef`, since `InvitationsRepository::create`/`findByCode` only ever carry a meeting's internal integer id, not its `meeting_ref` string — `MeetingService::issueClientToken` (Task 7) consumes `findById` directly.
- **Placeholder scan:** no step ships intentionally-broken or TBD code; every step's listing compiles standalone against the interfaces defined earlier in the plan.
