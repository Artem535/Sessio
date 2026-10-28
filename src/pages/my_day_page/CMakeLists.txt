cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_my_day_page)

find_package(Qt6 REQUIRED COMPONENTS Core)

qt_add_library(${TARGET_NAME} STATIC
        my_day_logic.cpp
        my_day_logic.h
)

add_dependencies(${TARGET_NAME}
        ${PROJECT_NAME}_database
        ${PROJECT_NAME}_meeting
)

target_link_libraries(${TARGET_NAME} PUBLIC
        ${PROJECT_NAME}_database
        ${PROJECT_NAME}_meeting
        Qt6::Core
)

target_include_directories(${TARGET_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
