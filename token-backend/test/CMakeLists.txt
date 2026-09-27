find_package(GTest REQUIRED)
# repository_concurrency_tests.cpp drives the repositories from std::thread.
find_package(Threads REQUIRED)

add_executable(token_backend_tests
  health_controller_tests.cpp
  sqlite_connection_tests.cpp
  config_tests.cpp
  hashing_tests.cpp
  livekit_jwt_tests.cpp
  accounts_repository_tests.cpp
  static_token_authorizer_tests.cpp
  meetings_repository_tests.cpp
  invitations_repository_tests.cpp
  repository_concurrency_tests.cpp
  meeting_service_tests.cpp
  http_integration_tests.cpp
)
target_link_libraries(token_backend_tests PRIVATE
  token_backend_lib
  GTest::gtest
  GTest::gtest_main
  Threads::Threads
)

include(GoogleTest)
gtest_discover_tests(token_backend_tests)
