// HTTP-layer integration tests.
//
// Everything else in this suite tests below the wire: MeetingService without
// HTTP, the repositories without a service. That left the actual contract —
// routing, header extraction, DTO field names, status-code mapping —
// unverified by anything automated, which is precisely where this project has
// shipped bugs (Task 1's unrouted endpoint, and findings I4 and I5 in the
// final review).
//
// These tests run the real controllers on a real oat++ server bound to a
// loopback test port and drive it with a real HTTP client, so a request here
// travels the same path a desktop client's would.

#include "auth/static_token_authorizer.h"
#include "config.h"
#include "controller/health_controller.h"
#include "controller/invitations_controller.h"
#include "controller/meetings_controller.h"
#include "db/accounts_repository.h"
#include "db/invitations_repository.h"
#include "db/meetings_repository.h"
#include "db/migrations.h"
#include "db/sqlite_connection.h"
#include "service/meeting_service.h"

#include "oatpp/network/Server.hpp"
#include "oatpp/network/tcp/client/ConnectionProvider.hpp"
#include "oatpp/network/tcp/server/ConnectionProvider.hpp"
#include "oatpp/parser/json/mapping/ObjectMapper.hpp"
#include "oatpp/web/client/HttpRequestExecutor.hpp"
#include "oatpp/web/protocol/http/outgoing/BufferBody.hpp"
#include "oatpp/web/server/HttpConnectionHandler.hpp"
#include "oatpp/web/server/HttpRouter.hpp"

#include <gtest/gtest.h>
#include <sodium.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <latch>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr v_uint16 kTestPort = 18432;
const char *kInvitationBase = "https://join.example.test/j/";

std::string isoFromNow(int64_t offsetSeconds) {
  auto when = std::chrono::system_clock::now() + std::chrono::seconds(offsetSeconds);
  std::time_t t = std::chrono::system_clock::to_time_t(when);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

// The responses are small and flat, so a substring probe is enough to assert
// on field names and values without pulling in a JSON parser. Asserting on the
// raw wire bytes is arguably the point: it catches a DTO field being renamed.
bool bodyHas(const std::string &body, const std::string &needle) {
  return body.find(needle) != std::string::npos;
}

std::string quotedField(const std::string &name, const std::string &value) {
  return "\"" + name + "\":\"" + value + "\"";
}

// Pulls a top-level string field out of a flat JSON object. oat++ escapes the
// slashes in URLs ("https:\/\/..."), which the callers below account for.
std::string jsonString(const std::string &body, const std::string &field) {
  const std::string key = "\"" + field + "\":\"";
  auto start = body.find(key);
  if (start == std::string::npos) {
    return {};
  }
  start += key.size();
  auto end = body.find('"', start);
  if (end == std::string::npos) {
    return {};
  }
  std::string raw = body.substr(start, end - start);
  std::string out;
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '\\' && i + 1 < raw.size()) {
      ++i;
    }
    out += raw[i];
  }
  return out;
}

struct HttpResponse {
  int status = 0;
  std::string body;
};

struct ConcurrentOutcome {
  std::vector<HttpResponse> responses;
  // Anything that should never happen: a 5xx, or a request that threw.
  std::vector<std::string> failures;

  int count(int status) const {
    return static_cast<int>(
        std::count_if(responses.begin(), responses.end(),
                       [status](const HttpResponse &r) { return r.status == status; }));
  }
};

// Fires `threadCount` identical requests as close to simultaneously as the
// platform allows: every thread builds its client first, then waits on a latch,
// so the requests overlap instead of each racing only whichever threads have
// already started.
//
// A client provider per thread on purpose. Sharing one would queue the requests
// onto a single keep-alive connection, which oat++ serves from a single worker
// thread — precisely the concurrency these tests need to create.
ConcurrentOutcome fireConcurrently(int threadCount, const char *method, const std::string &path,
                                    const oatpp::web::client::RequestExecutor::Headers &headers,
                                    const std::string &payload) {
  ConcurrentOutcome outcome;
  std::mutex outcomeMutex;
  std::latch start(1);

  std::vector<std::thread> threads;
  threads.reserve(static_cast<size_t>(threadCount));
  for (int i = 0; i < threadCount; ++i) {
    threads.emplace_back([&] {
      auto provider = oatpp::network::tcp::client::ConnectionProvider::createShared(
          {"127.0.0.1", kTestPort});
      auto executor = oatpp::web::client::HttpRequestExecutor::createShared(provider);
      std::shared_ptr<oatpp::web::protocol::http::outgoing::Body> body;
      if (!payload.empty()) {
        body = oatpp::web::protocol::http::outgoing::BufferBody::createShared(
            oatpp::String(payload.c_str()), "application/json");
      }
      auto ownHeaders = headers;

      start.wait();
      try {
        auto response = executor->execute(method, path.c_str(), ownHeaders, body, nullptr);
        HttpResponse result;
        result.status = response->getStatusCode();
        auto text = response->readBodyToString();
        result.body = text ? *text : std::string();

        std::lock_guard<std::mutex> lk(outcomeMutex);
        if (result.status >= 500) {
          outcome.failures.emplace_back("HTTP " + std::to_string(result.status) + ": " +
                                         result.body);
        }
        outcome.responses.push_back(std::move(result));
      } catch (const std::exception &e) {
        std::lock_guard<std::mutex> lk(outcomeMutex);
        outcome.failures.emplace_back(std::string("request threw: ") + e.what());
      }
    });
  }

  start.count_down();
  for (auto &t : threads) {
    t.join();
  }
  return outcome;
}

} // namespace

// One server for the whole file: starting and stopping an oat++ server per
// test would dominate the runtime, and each test creates its own meeting so
// they do not collide over shared state.
class HttpIntegrationTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    ASSERT_TRUE(sodium_init() >= 0);

    conn_ = new pcm::tokenbackend::SqliteConnection(":memory:");
    pcm::tokenbackend::runMigrations(*conn_);

    accounts_ = new pcm::tokenbackend::AccountsRepository(*conn_);
    credential_ = new std::string(accounts_->seedAccount());
    authorizer_ = new pcm::tokenbackend::StaticTokenAuthorizer(*accounts_);
    meetings_ = new pcm::tokenbackend::MeetingsRepository(*conn_);
    invitations_ = new pcm::tokenbackend::InvitationsRepository(*conn_);

    config_ = new pcm::tokenbackend::Config{};
    config_->liveKitApiKey = "test-key";
    config_->liveKitApiSecret = "test-secret";
    config_->tokenTtlSeconds = 600;

    service_ = new pcm::tokenbackend::MeetingService(*authorizer_, *meetings_, *invitations_,
                                                      *config_, "ws://livekit.test:7880");

    auto objectMapper = oatpp::parser::json::mapping::ObjectMapper::createShared();

    auto router = oatpp::web::server::HttpRouter::createShared();
    healthController_ = std::make_shared<pcm::tokenbackend::HealthController>();
    meetingsController_ = std::make_shared<pcm::tokenbackend::MeetingsController>(
        objectMapper, *service_, kInvitationBase);
    invitationsController_ =
        std::make_shared<pcm::tokenbackend::InvitationsController>(objectMapper, *service_);

    // Exactly the wiring main.cpp performs. If an endpoint is left unrouted
    // there — the Task 1 bug — these tests see the same 404 a client would.
    router->route(healthController_->getEndpoints());
    router->route(meetingsController_->getEndpoints());
    router->route(invitationsController_->getEndpoints());

    connectionHandler_ = oatpp::web::server::HttpConnectionHandler::createShared(router);
    serverProvider_ = oatpp::network::tcp::server::ConnectionProvider::createShared(
        {"127.0.0.1", kTestPort});
    server_ = new oatpp::network::Server(serverProvider_, connectionHandler_);

    serverThread_ = new std::thread([] { server_->run(); });

    clientProvider_ = oatpp::network::tcp::client::ConnectionProvider::createShared(
        {"127.0.0.1", kTestPort});
    executor_ = oatpp::web::client::HttpRequestExecutor::createShared(clientProvider_);

    // Wait for the listener rather than sleeping a fixed amount.
    for (int i = 0; i < 100; ++i) {
      try {
        auto response = executor_->execute("GET", "/healthz", {}, nullptr, nullptr);
        if (response->getStatusCode() == 200) {
          return;
        }
      } catch (...) {
        // not listening yet
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    FAIL() << "test server did not come up on port " << kTestPort;
  }

  static void TearDownTestSuite() {
    server_->stop();
    connectionHandler_->stop();
    serverProvider_->stop();
    // The accept loop wakes on the next connection attempt; nudge it so the
    // thread is joinable promptly instead of blocking the whole suite.
    try {
      executor_->execute("GET", "/healthz", {}, nullptr, nullptr);
    } catch (...) {
    }
    serverThread_->join();

    delete serverThread_;
    delete server_;
    delete service_;
    delete config_;
    delete invitations_;
    delete meetings_;
    delete authorizer_;
    delete credential_;
    delete accounts_;
    delete conn_;
  }

  static HttpResponse request(const char *method, const std::string &path,
                               const oatpp::web::client::RequestExecutor::Headers &headers = {},
                               const std::string &body = "") {
    std::shared_ptr<oatpp::web::protocol::http::outgoing::Body> outgoing;
    if (!body.empty()) {
      outgoing = oatpp::web::protocol::http::outgoing::BufferBody::createShared(
          oatpp::String(body.c_str()), "application/json");
    }
    auto response = executor_->execute(method, path.c_str(), headers, outgoing, nullptr);
    HttpResponse result;
    result.status = response->getStatusCode();
    auto text = response->readBodyToString();
    result.body = text ? *text : std::string();
    return result;
  }

  static oatpp::web::client::RequestExecutor::Headers authHeaders(const std::string &value) {
    oatpp::web::client::RequestExecutor::Headers headers;
    headers.put("Authorization", oatpp::String(value.c_str()));
    return headers;
  }

  static oatpp::web::client::RequestExecutor::Headers bearer() {
    return authHeaders("Bearer " + *credential_);
  }

  // Creates a meeting over HTTP and returns the parsed response.
  struct CreatedMeeting {
    std::string meetingRef;
    std::string invitationCode;
    std::string passcode;
    std::string body;
  };

  static CreatedMeeting createMeeting(int64_t startOffset = -60, int64_t endOffset = 60 * 60) {
    auto response = request("POST", "/v1/meetings", bearer(),
                             "{\"scheduledStart\":\"" + isoFromNow(startOffset) +
                                 "\",\"scheduledEnd\":\"" + isoFromNow(endOffset) + "\"}");
    EXPECT_EQ(response.status, 200) << response.body;
    CreatedMeeting created;
    created.body = response.body;
    created.meetingRef = jsonString(response.body, "meetingRef");
    created.passcode = jsonString(response.body, "passcode");
    auto url = jsonString(response.body, "invitationUrl");
    created.invitationCode = url.substr(url.rfind('/') + 1);
    return created;
  }

  static std::string clientTokenPath(const std::string &code) {
    return "/v1/invitations/" + code + "/client-token";
  }

  static pcm::tokenbackend::SqliteConnection *conn_;
  static pcm::tokenbackend::AccountsRepository *accounts_;
  static pcm::tokenbackend::Authorizer *authorizer_;
  static pcm::tokenbackend::MeetingsRepository *meetings_;
  static pcm::tokenbackend::InvitationsRepository *invitations_;
  static pcm::tokenbackend::Config *config_;
  static pcm::tokenbackend::MeetingService *service_;
  static std::string *credential_;

  static std::shared_ptr<pcm::tokenbackend::HealthController> healthController_;
  static std::shared_ptr<pcm::tokenbackend::MeetingsController> meetingsController_;
  static std::shared_ptr<pcm::tokenbackend::InvitationsController> invitationsController_;
  static std::shared_ptr<oatpp::web::server::HttpConnectionHandler> connectionHandler_;
  static std::shared_ptr<oatpp::network::tcp::server::ConnectionProvider> serverProvider_;
  static std::shared_ptr<oatpp::network::tcp::client::ConnectionProvider> clientProvider_;
  static std::shared_ptr<oatpp::web::client::HttpRequestExecutor> executor_;
  static oatpp::network::Server *server_;
  static std::thread *serverThread_;
};

pcm::tokenbackend::SqliteConnection *HttpIntegrationTest::conn_ = nullptr;
pcm::tokenbackend::AccountsRepository *HttpIntegrationTest::accounts_ = nullptr;
pcm::tokenbackend::Authorizer *HttpIntegrationTest::authorizer_ = nullptr;
pcm::tokenbackend::MeetingsRepository *HttpIntegrationTest::meetings_ = nullptr;
pcm::tokenbackend::InvitationsRepository *HttpIntegrationTest::invitations_ = nullptr;
pcm::tokenbackend::Config *HttpIntegrationTest::config_ = nullptr;
pcm::tokenbackend::MeetingService *HttpIntegrationTest::service_ = nullptr;
std::string *HttpIntegrationTest::credential_ = nullptr;
std::shared_ptr<pcm::tokenbackend::HealthController> HttpIntegrationTest::healthController_;
std::shared_ptr<pcm::tokenbackend::MeetingsController> HttpIntegrationTest::meetingsController_;
std::shared_ptr<pcm::tokenbackend::InvitationsController>
    HttpIntegrationTest::invitationsController_;
std::shared_ptr<oatpp::web::server::HttpConnectionHandler> HttpIntegrationTest::connectionHandler_;
std::shared_ptr<oatpp::network::tcp::server::ConnectionProvider>
    HttpIntegrationTest::serverProvider_;
std::shared_ptr<oatpp::network::tcp::client::ConnectionProvider>
    HttpIntegrationTest::clientProvider_;
std::shared_ptr<oatpp::web::client::HttpRequestExecutor> HttpIntegrationTest::executor_;
oatpp::network::Server *HttpIntegrationTest::server_ = nullptr;
std::thread *HttpIntegrationTest::serverThread_ = nullptr;

// --- routing ----------------------------------------------------------------

TEST_F(HttpIntegrationTest, HealthEndpointIsRouted) {
  auto response = request("GET", "/healthz");
  EXPECT_EQ(response.status, 200);
  EXPECT_EQ(response.body, "ok");
}

TEST_F(HttpIntegrationTest, UnknownPathIs404) {
  EXPECT_EQ(request("GET", "/v1/nope").status, 404);
}

// --- POST /v1/meetings ------------------------------------------------------

TEST_F(HttpIntegrationTest, CreateMeetingReturnsEveryDocumentedField) {
  auto start = isoFromNow(-60);
  auto end = isoFromNow(60 * 60);
  auto response = request("POST", "/v1/meetings", bearer(),
                           "{\"scheduledStart\":\"" + start + "\",\"scheduledEnd\":\"" + end +
                               "\"}");

  ASSERT_EQ(response.status, 200) << response.body;
  EXPECT_TRUE(bodyHas(response.body, "\"meetingRef\":\"mtg_")) << response.body;
  EXPECT_TRUE(bodyHas(response.body, "\"invitationUrl\":\"")) << response.body;
  EXPECT_TRUE(bodyHas(response.body, "\"passcode\":\"")) << response.body;
  EXPECT_TRUE(bodyHas(response.body, quotedField("scheduledStart", start))) << response.body;
  EXPECT_TRUE(bodyHas(response.body, quotedField("scheduledEnd", end))) << response.body;

  EXPECT_EQ(jsonString(response.body, "passcode").size(), 6u);
  EXPECT_EQ(jsonString(response.body, "invitationUrl").rfind(kInvitationBase, 0), 0u)
      << "invitationUrl must be built from INVITATION_BASE_URL";
}

TEST_F(HttpIntegrationTest, CreateMeetingWithNoAuthorizationHeaderIs401) {
  auto response = request("POST", "/v1/meetings", {},
                           "{\"scheduledStart\":\"" + isoFromNow(-60) + "\",\"scheduledEnd\":\"" +
                               isoFromNow(60 * 60) + "\"}");
  EXPECT_EQ(response.status, 401) << response.body;
  EXPECT_TRUE(bodyHas(response.body, "\"error\"")) << response.body;
}

TEST_F(HttpIntegrationTest, CreateMeetingWithWrongCredentialIs401) {
  auto response = request("POST", "/v1/meetings", authHeaders("Bearer not-the-credential"),
                           "{\"scheduledStart\":\"" + isoFromNow(-60) + "\",\"scheduledEnd\":\"" +
                               isoFromNow(60 * 60) + "\"}");
  EXPECT_EQ(response.status, 401) << response.body;
}

TEST_F(HttpIntegrationTest, CreateMeetingWithoutASchedduleIs400) {
  auto response = request("POST", "/v1/meetings", bearer(), "{}");
  EXPECT_EQ(response.status, 400) << response.body;
  EXPECT_TRUE(bodyHas(response.body, "schedule_required")) << response.body;
}

// I5: the RFC 7235 form used to hash the literal "Bearer <cred>" and 401.
TEST_F(HttpIntegrationTest, BothBearerAndBareCredentialFormsAreAccepted) {
  const std::string payload = "{\"scheduledStart\":\"" + isoFromNow(-60) +
                               "\",\"scheduledEnd\":\"" + isoFromNow(60 * 60) + "\"}";

  EXPECT_EQ(request("POST", "/v1/meetings", authHeaders("Bearer " + *credential_), payload).status,
            200);
  EXPECT_EQ(request("POST", "/v1/meetings", authHeaders("bearer " + *credential_), payload).status,
            200)
      << "the scheme token is case-insensitive per RFC 7235";
  EXPECT_EQ(request("POST", "/v1/meetings", authHeaders(*credential_), payload).status, 200)
      << "the bare form must keep working";
}

// --- POST /v1/meetings/{ref}/specialist-token -------------------------------

TEST_F(HttpIntegrationTest, SpecialistTokenReturnsTheDocumentedFields) {
  auto meeting = createMeeting();
  auto response =
      request("POST", "/v1/meetings/" + meeting.meetingRef + "/specialist-token", bearer());

  ASSERT_EQ(response.status, 200) << response.body;
  // oat++ escapes the slashes in a JSON string, so compare the decoded value.
  EXPECT_EQ(jsonString(response.body, "endpointUrl"), "ws://livekit.test:7880") << response.body;
  EXPECT_TRUE(bodyHas(response.body, "\"roomName\":\"rm_")) << response.body;
  EXPECT_TRUE(bodyHas(response.body, "\"token\":\"")) << response.body;
  EXPECT_TRUE(bodyHas(response.body, "\"expiresAt\":")) << response.body;
  EXPECT_TRUE(bodyHas(response.body, "\"token\":\"eyJ"))
      << "the token must be a JWT: " << response.body;
}

TEST_F(HttpIntegrationTest, SpecialistTokenForAnUnknownMeetingIs404) {
  EXPECT_EQ(request("POST", "/v1/meetings/mtg_does_not_exist/specialist-token", bearer()).status,
            404);
}

TEST_F(HttpIntegrationTest, SpecialistTokenWithoutCredentialIs401) {
  auto meeting = createMeeting();
  EXPECT_EQ(request("POST", "/v1/meetings/" + meeting.meetingRef + "/specialist-token").status,
            401);
}

// --- POST /v1/invitations/{code}/client-token -------------------------------

TEST_F(HttpIntegrationTest, ClientTokenSucceedsWithNoCredentialAtAll) {
  auto meeting = createMeeting();
  auto response = request("POST", clientTokenPath(meeting.invitationCode), {},
                           "{\"passcode\":\"" + meeting.passcode + "\"}");

  ASSERT_EQ(response.status, 200) << response.body;
  EXPECT_TRUE(bodyHas(response.body, "\"token\":\"eyJ")) << response.body;
  EXPECT_TRUE(bodyHas(response.body, "\"roomName\":\"rm_")) << response.body;
  EXPECT_EQ(jsonString(response.body, "endpointUrl"), "ws://livekit.test:7880") << response.body;
}

TEST_F(HttpIntegrationTest, ClientTokenWithWrongPasscodeIs401) {
  auto meeting = createMeeting();
  auto response =
      request("POST", clientTokenPath(meeting.invitationCode), {}, "{\"passcode\":\"000000\"}");
  EXPECT_EQ(response.status, 401) << response.body;
}

TEST_F(HttpIntegrationTest, ClientTokenWithUnknownCodeIs404) {
  auto response = request("POST", clientTokenPath("no-such-code"), {}, "{\"passcode\":\"123456\"}");
  EXPECT_EQ(response.status, 404) << response.body;
}

// I4.1: `{}` used to reach the service as an empty passcode and be scored as a
// wrong guess, so six of them permanently killed the invitation.
TEST_F(HttpIntegrationTest, ClientTokenWithMissingPasscodeIs400AndCostsNoAttempt) {
  auto meeting = createMeeting();

  for (int i = 0; i < 7; ++i) {
    auto response = request("POST", clientTokenPath(meeting.invitationCode), {}, "{}");
    EXPECT_EQ(response.status, 400) << "request " << i << ": " << response.body;
    EXPECT_TRUE(bodyHas(response.body, "passcode_required")) << response.body;
  }

  auto stillWorks = request("POST", clientTokenPath(meeting.invitationCode), {},
                             "{\"passcode\":\"" + meeting.passcode + "\"}");
  EXPECT_EQ(stillWorks.status, 200)
      << "malformed requests must not burn the 5-attempt budget: " << stillWorks.body;
}

TEST_F(HttpIntegrationTest, ClientTokenWithEmptyPasscodeStringIs400) {
  auto meeting = createMeeting();
  auto response =
      request("POST", clientTokenPath(meeting.invitationCode), {}, "{\"passcode\":\"\"}");
  EXPECT_EQ(response.status, 400) << response.body;
}

TEST_F(HttpIntegrationTest, FiveWrongPasscodesLockTheInvitationOutWith429) {
  auto meeting = createMeeting();

  for (int i = 0; i < 5; ++i) {
    request("POST", clientTokenPath(meeting.invitationCode), {}, "{\"passcode\":\"000000\"}");
  }

  auto response = request("POST", clientTokenPath(meeting.invitationCode), {},
                           "{\"passcode\":\"" + meeting.passcode + "\"}");
  EXPECT_EQ(response.status, 429) << response.body;
}

TEST_F(HttpIntegrationTest, InvitationCodeStaysRedeemableForReconnects) {
  auto meeting = createMeeting();
  auto path = clientTokenPath(meeting.invitationCode);
  const std::string body = "{\"passcode\":\"" + meeting.passcode + "\"}";

  EXPECT_EQ(request("POST", path, {}, body).status, 200);
  EXPECT_EQ(request("POST", path, {}, body).status, 200)
      << "the invitation is not consumed by first use";
}

// --- I3: scheduled-window enforcement over the wire -------------------------

TEST_F(HttpIntegrationTest, TokensOutsideTheScheduledWindowAre410) {
  auto future = createMeeting(3 * 60 * 60, 4 * 60 * 60);

  EXPECT_EQ(request("POST", clientTokenPath(future.invitationCode), {},
                     "{\"passcode\":\"" + future.passcode + "\"}")
                .status,
            410);
  EXPECT_EQ(
      request("POST", "/v1/meetings/" + future.meetingRef + "/specialist-token", bearer()).status,
      410);

  auto past = createMeeting(-4 * 60 * 60, -3 * 60 * 60);
  EXPECT_EQ(request("POST", clientTokenPath(past.invitationCode), {},
                     "{\"passcode\":\"" + past.passcode + "\"}")
                .status,
            410);
}

// --- POST /v1/meetings/{ref}/invitation (I4.2) ------------------------------

TEST_F(HttpIntegrationTest, ReissueInvitationRetiresTheOldCodeAndKeepsTheMeeting) {
  auto meeting = createMeeting();

  auto response = request("POST", "/v1/meetings/" + meeting.meetingRef + "/invitation", bearer());
  ASSERT_EQ(response.status, 200) << response.body;
  EXPECT_TRUE(bodyHas(response.body, quotedField("meetingRef", meeting.meetingRef)))
      << response.body;

  auto newUrl = jsonString(response.body, "invitationUrl");
  auto newCode = newUrl.substr(newUrl.rfind('/') + 1);
  auto newPasscode = jsonString(response.body, "passcode");
  EXPECT_NE(newCode, meeting.invitationCode);

  EXPECT_EQ(request("POST", clientTokenPath(meeting.invitationCode), {},
                     "{\"passcode\":\"" + meeting.passcode + "\"}")
                .status,
            410)
      << "the superseded code must stop working";
  EXPECT_EQ(
      request("POST", clientTokenPath(newCode), {}, "{\"passcode\":\"" + newPasscode + "\"}")
          .status,
      200);
}

TEST_F(HttpIntegrationTest, ReissueRecoversAnInvitationFromTheLockout) {
  auto meeting = createMeeting();
  for (int i = 0; i < 5; ++i) {
    request("POST", clientTokenPath(meeting.invitationCode), {}, "{\"passcode\":\"000000\"}");
  }
  ASSERT_EQ(request("POST", clientTokenPath(meeting.invitationCode), {},
                     "{\"passcode\":\"" + meeting.passcode + "\"}")
                .status,
            429);

  auto response = request("POST", "/v1/meetings/" + meeting.meetingRef + "/invitation", bearer());
  ASSERT_EQ(response.status, 200) << response.body;
  auto newUrl = jsonString(response.body, "invitationUrl");
  auto newCode = newUrl.substr(newUrl.rfind('/') + 1);

  EXPECT_EQ(request("POST", clientTokenPath(newCode), {},
                     "{\"passcode\":\"" + jsonString(response.body, "passcode") + "\"}")
                .status,
            200);
}

TEST_F(HttpIntegrationTest, ReissueWithoutCredentialIs401AndUnknownMeetingIs404) {
  auto meeting = createMeeting();
  EXPECT_EQ(request("POST", "/v1/meetings/" + meeting.meetingRef + "/invitation").status, 401);
  EXPECT_EQ(request("POST", "/v1/meetings/mtg_does_not_exist/invitation", bearer()).status, 404);
}

// --- POST /v1/meetings/{ref}/invalidate -------------------------------------

TEST_F(HttpIntegrationTest, InvalidateReturns204AndStopsBothTokenEndpoints) {
  auto meeting = createMeeting();

  auto response = request("POST", "/v1/meetings/" + meeting.meetingRef + "/invalidate", bearer());
  EXPECT_EQ(response.status, 204) << response.body;
  EXPECT_TRUE(response.body.empty()) << "204 must carry no body, got: " << response.body;

  EXPECT_EQ(
      request("POST", "/v1/meetings/" + meeting.meetingRef + "/specialist-token", bearer()).status,
      410);
  EXPECT_EQ(request("POST", clientTokenPath(meeting.invitationCode), {},
                     "{\"passcode\":\"" + meeting.passcode + "\"}")
                .status,
            410);
}

TEST_F(HttpIntegrationTest, InvalidateWithoutCredentialIs401AndUnknownMeetingIs404) {
  auto meeting = createMeeting();
  EXPECT_EQ(request("POST", "/v1/meetings/" + meeting.meetingRef + "/invalidate").status, 401);
  EXPECT_EQ(request("POST", "/v1/meetings/mtg_does_not_exist/invalidate", bearer()).status, 404);
}

// --- concurrency ------------------------------------------------------------

// These two are the end-to-end counterpart to repository_concurrency_tests.cpp:
// real HTTP requests arriving at the same time on separate connections, so
// oat++ really does dispatch them onto different worker threads sharing the one
// SqliteConnection. That is the arrangement in which the connection's lock has
// to hold.
//
// Worth knowing which of the two actually pins the bug. Verified by making
// SqliteConnection::lock() a no-op: the reissue test below fails immediately
// (500s, "cannot start a transaction within a transaction"), because
// reissueForMeeting hashes the new passcode *inside* its transaction, holding
// the transaction open for the ~60ms an Argon2id hash takes and making an
// overlap near-certain. The wrong-passcode test keeps passing without the lock,
// because there the Argon2id verify happens *before* the transaction and
// incidentally staggers the threads past each other. It is kept for the
// end-to-end cap assertion it makes, not as a race detector.
//
// The target below is still the worst case in the API by exposure: POST
// /v1/invitations/{code}/client-token is unauthenticated, so anyone can drive
// it concurrently, and a wrong passcode there runs the transactional
// read-modify-write enforcing ADR-12's 5-attempt cap.
//
// The expected outcome is exact rather than approximate. Each successful
// increment returns a distinct count, so the values 1..4 each go to exactly one
// request and those four get 401; every other request either sees a count of 5
// or more, or arrives after the auto-invalidate and is turned away on the
// invitation's status. So: exactly four 401s and the rest 429, whatever order
// the threads happen to run in — and no 500 at all.
TEST_F(HttpIntegrationTest, ConcurrentWrongPasscodesNeverError500AndKeepTheAttemptCapExact) {
  auto meeting = createMeeting();
  const std::string path = clientTokenPath(meeting.invitationCode);
  // Guard against the 1-in-a-million case where the generated passcode is the
  // one this test guesses with; a match would make every assertion below wrong.
  const std::string wrongPasscode = meeting.passcode == "000000" ? "111111" : "000000";
  const std::string payload = "{\"passcode\":\"" + wrongPasscode + "\"}";

  constexpr int kThreads = 10;
  auto outcome = fireConcurrently(kThreads, "POST", path, {}, payload);

  EXPECT_TRUE(outcome.failures.empty())
      << "first failure: " << (outcome.failures.empty() ? "" : outcome.failures.front());
  ASSERT_EQ(outcome.responses.size(), static_cast<size_t>(kThreads));

  EXPECT_EQ(outcome.count(401), 4) << "exactly four requests may burn attempts 1-4";
  EXPECT_EQ(outcome.count(429), kThreads - 4)
      << "every other request must be refused, not served";

  // The cap actually closed the invitation, and the good passcode no longer
  // works either.
  EXPECT_EQ(request("POST", path, {}, "{\"passcode\":\"" + meeting.passcode + "\"}").status, 429);
}

// This is the one that catches the bug end-to-end. reissueForMeeting retires
// every active invitation for a meeting and mints a replacement as one
// transaction, and the mint hashes the new passcode with Argon2id *inside* that
// transaction — so the transaction stays open for tens of milliseconds and
// concurrent reissues are all but guaranteed to overlap. Without the lock the
// second BEGIN IMMEDIATE to arrive fails with SQLITE_ERROR ("cannot start a
// transaction within a transaction"), which no busy timeout can retry, and the
// endpoint 500s.
//
// The end state matters as much as the status codes: whichever order the
// reissues ran in, the practitioner must be left with exactly one invitation
// that works. Several would mean a superseded code still lets a client in;
// none would mean a retire landed after the last mint and locked everybody out.
TEST_F(HttpIntegrationTest, ConcurrentReissuesNeverError500AndLeaveOneWorkingInvitation) {
  auto meeting = createMeeting();

  constexpr int kThreads = 6;
  auto outcome = fireConcurrently(kThreads, "POST",
                                   "/v1/meetings/" + meeting.meetingRef + "/invitation", bearer(),
                                   "");

  EXPECT_TRUE(outcome.failures.empty())
      << "first failure: " << (outcome.failures.empty() ? "" : outcome.failures.front());
  ASSERT_EQ(outcome.responses.size(), static_cast<size_t>(kThreads));
  EXPECT_EQ(outcome.count(200), kThreads) << "every reissue must succeed";

  // Exactly one of the issued invitations may still mint a client token — plus
  // the original, which every reissue should have retired.
  int working = 0;
  for (const auto &response : outcome.responses) {
    auto url = jsonString(response.body, "invitationUrl");
    ASSERT_FALSE(url.empty()) << response.body;
    auto code = url.substr(url.rfind('/') + 1);
    auto passcode = jsonString(response.body, "passcode");
    if (request("POST", clientTokenPath(code), {}, "{\"passcode\":\"" + passcode + "\"}").status ==
        200) {
      ++working;
    }
  }
  EXPECT_EQ(working, 1) << "a reissue must leave exactly one usable invitation";
  EXPECT_EQ(request("POST", clientTokenPath(meeting.invitationCode), {},
                     "{\"passcode\":\"" + meeting.passcode + "\"}")
                .status,
            410)
      << "the original invitation must have been retired";
}
