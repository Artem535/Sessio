cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_my_day_page)

find_package(Qt6 REQUIRED COMPONENTS Core Qml Widgets Quick QuickWidgets)

qt_add_library(${TARGET_NAME} STATIC
        my_day_logic.cpp
        my_day_logic.h
        my_day_bridge.cpp
        my_day_bridge.h
        my_day_page.cpp
        my_day_page.h
)
set_target_properties(${TARGET_NAME} PROPERTIES AUTOMOC ON)

qt_add_resources(${TARGET_NAME} my_day_qml
        PREFIX "/sessio/myday"
        BASE "qml"
        FILES
        qml/MyDay.qml
        qml/StatTile.qml
        qml/TimelineCard.qml
        qml/TodoColumn.qml
        qml/EventListPopup.qml
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
        Qt6::Widgets
        Qt6::Quick
        Qt6::QuickWidgets
)

target_include_directories(${TARGET_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
