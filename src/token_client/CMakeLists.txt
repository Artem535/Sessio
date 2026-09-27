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
