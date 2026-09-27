cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_meeting)

find_package(Qt6 REQUIRED COMPONENTS Core)

qt_add_library(${TARGET_NAME} STATIC
        provider_kind.h
        meeting_descriptor.h
        meeting_provider.h
        external_url_meeting_provider.h
        external_url_meeting_provider.cpp
        livekit_meeting_provider.h
        livekit_meeting_provider.cpp
        meeting_coordinator.h
        meeting_coordinator.cpp
)

target_link_libraries(${TARGET_NAME} PUBLIC
        Qt6::Core
        ${PROJECT_NAME}_token_client
)

target_include_directories(${TARGET_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
