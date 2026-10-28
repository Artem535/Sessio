cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_my_day_page)

find_package(Qt6 REQUIRED COMPONENTS Core Qml)

qt_add_library(${TARGET_NAME} STATIC
        my_day_logic.cpp
        my_day_logic.h
        my_day_bridge.cpp
        my_day_bridge.h
)

add_dependencies(${TARGET_NAME}
        ${PROJECT_NAME}_database
        ${PROJECT_NAME}_meeting
        ${PROJECT_NAME}_event_view
        ${PROJECT_NAME}_widgets
)

target_link_libraries(${TARGET_NAME} PUBLIC
        ${PROJECT_NAME}_database
        ${PROJECT_NAME}_meeting
        ${PROJECT_NAME}_event_view
        ${PROJECT_NAME}_widgets
        Qt6::Core
        Qt6::Qml
)

target_include_directories(${TARGET_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
