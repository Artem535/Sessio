cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_webcall)

find_package(Qt6 REQUIRED COMPONENTS Core Network WebChannel WebEngineCore WebEngineWidgets)

qt_add_library(${TARGET_NAME} STATIC
        webcall_version.h
        webcall_version.cpp
)

target_link_libraries(${TARGET_NAME} PUBLIC
        Qt6::Core
        Qt6::Network
        Qt6::WebChannel
        Qt6::WebEngineCore
        Qt6::WebEngineWidgets
)

target_include_directories(${TARGET_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
