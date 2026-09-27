cmake_minimum_required(VERSION 3.28)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_calls_page)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Multimedia MultimediaWidgets)

qt_add_library(${TARGET_NAME} STATIC
        call_entry_widget.h
        call_entry_widget.cpp
        device_check_widget.h
        device_check_widget.cpp
        call_page.h
        call_page.cpp
)

target_link_libraries(${TARGET_NAME} PUBLIC
        Qt6::Core
        Qt6::Widgets
        Qt6::Multimedia
        Qt6::MultimediaWidgets
        ${PROJECT_NAME}_video
)
target_include_directories(${TARGET_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
