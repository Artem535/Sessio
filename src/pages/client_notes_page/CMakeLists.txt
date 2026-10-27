cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_client_notes_page)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets QuickWidgets Qml)

qt_add_library(${TARGET_NAME} STATIC
        client_notes_page.cpp
        client_notes_journal.cpp
        client_notes_workspace.cpp
        client_notes_page.h
        change_log_text.cpp
        change_log_text.h
)
qt_add_resources(${TARGET_NAME} client_notes_qml
  PREFIX "/sessio/journal"
  BASE "qml"
  FILES qml/ClientNotesWorkspace.qml)

target_include_directories(${TARGET_NAME} PRIVATE
        ${qlementine_SOURCE_DIR}/lib/include
)

add_dependencies(${TARGET_NAME}
        ${PROJECT_NAME}_database
        ${PROJECT_NAME}_widgets
        ${PROJECT_NAME}_event_view
)

target_link_libraries(${TARGET_NAME} PUBLIC
        ${PROJECT_NAME}_database
        ${PROJECT_NAME}_widgets
        ${PROJECT_NAME}_event_view
        qlementine
        Qt6::Widgets
        Qt6::PrintSupport
        Qt6::QuickWidgets
        Qt6::Qml
)

target_include_directories(${TARGET_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
