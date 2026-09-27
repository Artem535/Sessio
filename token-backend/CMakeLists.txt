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
  src/controller/dto.h
  src/controller/request_log.h
  src/controller/meetings_controller.h
  src/controller/invitations_controller.h
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
set_target_properties(token_backend_lib PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(token_backend_lib PUBLIC src)
target_link_libraries(token_backend_lib PUBLIC
  oatpp::oatpp
  unofficial-sodium::sodium
  unofficial::sqlite3::sqlite3
)

add_executable(pcm_token_backend src/main.cpp)
target_link_libraries(pcm_token_backend PRIVATE token_backend_lib)

if(PCM_TB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()
