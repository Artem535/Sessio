cmake_minimum_required(VERSION 3.28)

# Set the C++ standard
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_app)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
find_package(unofficial-sodium CONFIG REQUIRED)

# Add the library for the app module
qt_add_library(${TARGET_NAME} STATIC
        application.cpp
        main_window.cpp
        settings_dialog.cpp
        auto_backup_scheduler.cpp
        backup_encryption_policy.cpp
        credential_store.cpp
        token_backend_credential_store.cpp
        store_credential_reader.cpp
        keychain_series_invitation_store.cpp
        app_lock_service.cpp
        app_lock_controller.cpp
        app_lock_dialog.cpp
        role_selection_dialog.cpp
        client_mode_window.cpp
        client_mode_settings_dialog.cpp
        sessio_url.cpp
        single_instance_guard.cpp
        app_role_switcher.cpp
        application_restarter.cpp
        role_switch_prompt.cpp
)

target_include_directories(${TARGET_NAME} PRIVATE
        ${qlementine_SOURCE_DIR}/lib/include
)


qt_add_ui(${TARGET_NAME} SOURCES
        ../../ui/app/mainwindow.ui
        INCLUDE_PREFIX ui/app
        OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/generated_ui
)

add_dependencies(${TARGET_NAME}
        ${PROJECT_NAME}_config
        ${PROJECT_NAME}_database
        ${PROJECT_NAME}_backup
        ${PROJECT_NAME}_client_page
        ${PROJECT_NAME}_client_info_card_page
        ${PROJECT_NAME}_client_notes_page
        ${PROJECT_NAME}_analytics_page
        ${PROJECT_NAME}_event_page
        ${PROJECT_NAME}_meeting
        ${PROJECT_NAME}_widgets
        ${PROJECT_NAME}_calls_page
        ${PROJECT_NAME}_token_client

)

# Link Slint to the module
target_link_libraries(${TARGET_NAME} PUBLIC
        ${PROJECT_NAME}_config
        ${PROJECT_NAME}_database
        ${PROJECT_NAME}_backup
        ${PROJECT_NAME}_client_page
        ${PROJECT_NAME}_client_info_card_page
        ${PROJECT_NAME}_client_notes_page
        ${PROJECT_NAME}_analytics_page
        ${PROJECT_NAME}_event_page
        ${PROJECT_NAME}_meeting
        ${PROJECT_NAME}_widgets
        ${PROJECT_NAME}_calls_page
        ${PROJECT_NAME}_token_client
        qlementine
        Qt6::Widgets
        Qt6::Network
        qt6keychain
        unofficial-sodium::sodium
)

target_include_directories(${TARGET_NAME} PRIVATE
        ${qtkeychain_SOURCE_DIR}
        ${qtkeychain_BINARY_DIR}
)
