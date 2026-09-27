// token-backend/src/main.cpp
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
#include "oatpp/network/tcp/server/ConnectionProvider.hpp"
#include "oatpp/parser/json/mapping/ObjectMapper.hpp"
#include "oatpp/web/server/HttpConnectionHandler.hpp"
#include "oatpp/web/server/HttpRouter.hpp"

#include <cstdlib>
#include <iostream>
#include <sodium.h>

int main(int argc, char **argv) {
  oatpp::base::Environment::init();

  if (sodium_init() < 0) {
    std::cerr << "Fatal: libsodium initialization failed" << std::endl;
    return 1;
  }

  if (argc > 1 && std::string(argv[1]) == "--seed-account") {
    // Deliberately does not go through Config::fromEnv(): seeding touches only
    // the database, and demanding the LiveKit and invitation-URL variables
    // here would make the one-off bootstrap step fail for reasons that have
    // nothing to do with it.
    const char *dbPathEnv = std::getenv("DB_PATH");
    pcm::tokenbackend::SqliteConnection conn(dbPathEnv && dbPathEnv[0] != '\0'
                                                  ? dbPathEnv
                                                  : "token-backend.sqlite3");
    pcm::tokenbackend::runMigrations(conn);
    pcm::tokenbackend::AccountsRepository accounts(conn);
    auto credential = accounts.seedAccount();
    std::cout << "Seeded account. Bearer credential (copy this now, it will not be shown again):\n"
              << credential << std::endl;
    oatpp::base::Environment::destroy();
    return 0;
  }

  {
    auto config = pcm::tokenbackend::Config::fromEnv();
    pcm::tokenbackend::SqliteConnection conn(config.dbPath);
    pcm::tokenbackend::runMigrations(conn);

    pcm::tokenbackend::AccountsRepository accounts(conn);
    pcm::tokenbackend::StaticTokenAuthorizer authorizer(accounts);
    pcm::tokenbackend::MeetingsRepository meetings(conn);
    pcm::tokenbackend::InvitationsRepository invitations(conn);

    pcm::tokenbackend::MeetingService service(authorizer, meetings, invitations, config,
                                               config.liveKitWsEndpoint);

    auto objectMapper = oatpp::parser::json::mapping::ObjectMapper::createShared();

    auto router = oatpp::web::server::HttpRouter::createShared();
    auto healthController = std::make_shared<pcm::tokenbackend::HealthController>();
    auto meetingsController = std::make_shared<pcm::tokenbackend::MeetingsController>(
        objectMapper, service, config.invitationBaseUrl);
    auto invitationsController =
        std::make_shared<pcm::tokenbackend::InvitationsController>(objectMapper, service);

    // Register controller endpoints on the router
    router->route(healthController->getEndpoints());
    router->route(meetingsController->getEndpoints());
    router->route(invitationsController->getEndpoints());

    auto connectionHandler = oatpp::web::server::HttpConnectionHandler::createShared(router);
    const char *portEnv = std::getenv("PORT");
    v_uint16 port = portEnv ? static_cast<v_uint16>(std::atoi(portEnv)) : 8080;
    auto connectionProvider =
        oatpp::network::tcp::server::ConnectionProvider::createShared({"0.0.0.0", port});

    oatpp::network::Server server(connectionProvider, connectionHandler);
    // Startup goes through the same logger as the per-request lines so an
    // operator reading the log can see where a restart falls among them.
    OATPP_LOGI("pcm-token-backend", "listening on :%d db=%s ttl=%ds", static_cast<int>(port),
               config.dbPath.c_str(), config.tokenTtlSeconds);
    server.run();
  }

  oatpp::base::Environment::destroy();
  return 0;
}
