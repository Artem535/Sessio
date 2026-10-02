cmake_minimum_required(VERSION 3.28)

# Set the C++ standard
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_event_page)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets PrintSupport Gui)

# Add the library for the app module
qt_add_library(${TARGET_NAME} STATIC
        event_info.cpp
        qevent_details_widget.cpp
        qevent_details_widget.h
        day_summary_widget.cpp
        day_summary_widget.h
        series_call_status_text.cpp
        series_call_status_text.h
)

target_include_directories(${TARGET_NAME} PRIVATE
        ${qlementine_SOURCE_DIR}/lib/include
)

qt_add_ui(${TARGET_NAME} SOURCES
        ../../../ui/pages/eventinfo.ui
        ../../../ui/pages/eventdetails.ui
        INCLUDE_PREFIX ui/pages
)

add_dependencies(${TARGET_NAME}
        ${PROJECT_NAME}_config
        ${PROJECT_NAME}_database
        ${PROJECT_NAME}_client_model
        ${PROJECT_NAME}_widgets
        ${PROJECT_NAME}_event_view
        ${PROJECT_NAME}_timeline_widget
        ${PROJECT_NAME}_meeting
)

# Link Slint to the module
target_link_libraries(${TARGET_NAME} PUBLIC
        ${PROJECT_NAME}_config
        ${PROJECT_NAME}_database
        ${PROJECT_NAME}_client_model
        ${PROJECT_NAME}_widgets
        ${PROJECT_NAME}_event_view
        ${PROJECT_NAME}_timeline_widget
        ${PROJECT_NAME}_meeting
        qlementine
        Qt6::Widgets
        Qt6::Gui
)

target_include_directories(${TARGET_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
