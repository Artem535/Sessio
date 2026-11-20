cmake_minimum_required(VERSION 3.28)

# Set the C++ standard
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_widgets)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# Add the library for the app module
qt_add_library(${TARGET_NAME} STATIC
        app_settings.cpp
        notification_text.cpp
        meeting_utils.cpp
        meeting_utils.h
        sensitive_clipboard.cpp
        sensitive_clipboard.h
        tab_button.cpp
        rounded_calendar_widget.cpp
        quick_slots_widget.cpp
        busy_spinner.cpp
        busy_spinner.h
        call_control_icons.cpp
        call_control_icons.h
        combo_fit.cpp
        combo_fit.h
)

#add_dependencies(${TARGET_NAME}
#)

# Link Slint to the module
target_link_libraries(${TARGET_NAME} PUBLIC
        Qt6::Widgets
        Qt6::Gui
)

target_include_directories(${TARGET_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
# transcription_tuning.h is a Qt-free, header-only value type shared with the speech engine.
target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/src/transcription)
