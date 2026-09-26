cmake_minimum_required(VERSION 3.28)
project(PCM_Tests VERSION 0.1 LANGUAGES CXX)

enable_testing()
find_package(GTest REQUIRED)
include(GoogleTest)

# Find Poco
find_package(Poco REQUIRED COMPONENTS Foundation Zip)

# Config tests
add_executable(Sessio_config_tests config_tests.cpp)
target_link_libraries(Sessio_config_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_config
)
gtest_discover_tests(Sessio_config_tests)

# Database tests
add_executable(Sessio_database_tests database_tests.cpp)
target_link_libraries(Sessio_database_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_config
    Sessio_database
    Poco::Foundation
)
gtest_discover_tests(Sessio_database_tests)

# Schedule conflict tests
add_executable(Sessio_schedule_conflict_tests
    schedule_conflict_service_tests.cpp
)
target_link_libraries(Sessio_schedule_conflict_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_event_view
)
gtest_discover_tests(Sessio_schedule_conflict_tests)

# Recurrence utils tests
add_executable(Sessio_recurrence_tests recurrence_utils_tests.cpp)
target_link_libraries(Sessio_recurrence_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_config
    Sessio_database
    Sessio_event_view
    Poco::Foundation
)
gtest_discover_tests(Sessio_recurrence_tests)

# Change-log line text tests
add_executable(Sessio_change_log_text_tests change_log_text_tests.cpp)
target_link_libraries(Sessio_change_log_text_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_config
    Sessio_database
    Sessio_client_notes_page
    Poco::Foundation
)
gtest_discover_tests(Sessio_change_log_text_tests)

# Notification text tests
add_executable(Sessio_notification_text_tests
    notification_text_tests.cpp
    ../src/widgets/notification_text.cpp
)
target_include_directories(Sessio_notification_text_tests PRIVATE
    ../src/widgets
)
target_link_libraries(Sessio_notification_text_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Core
)
gtest_discover_tests(Sessio_notification_text_tests)

# Provider kind tests
add_executable(Sessio_provider_kind_tests provider_kind_tests.cpp)
target_link_libraries(Sessio_provider_kind_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_meeting
)
gtest_discover_tests(Sessio_provider_kind_tests)

# Audio chunker tests (framework-free, no Qt/LiveKit needed)
add_executable(Sessio_audio_chunker_tests
    ${CMAKE_SOURCE_DIR}/src/video/audio_chunker.cpp
    audio_chunker_tests.cpp
)
target_include_directories(Sessio_audio_chunker_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_audio_chunker_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(Sessio_audio_chunker_tests)

# Frame conversion tests (needs Qt::Gui for QImage, and LiveKit for VideoFrame)
add_executable(Sessio_frame_convert_tests
    ${CMAKE_SOURCE_DIR}/src/video/frame_convert.cpp
    frame_convert_tests.cpp
)
target_include_directories(Sessio_frame_convert_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_frame_convert_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Core
    Qt6::Gui
    LiveKit::livekit
)
gtest_discover_tests(Sessio_frame_convert_tests)

# External URL meeting provider tests
add_executable(Sessio_external_url_meeting_provider_tests
    external_url_meeting_provider_tests.cpp
    meeting_provider_test_listener.h
)
target_link_libraries(Sessio_external_url_meeting_provider_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_meeting
)
# meeting_provider_test_listener.h defines a Q_OBJECT class shared with
# Sessio_meeting_coordinator_tests; listing it as a source (rather than only
# #include-ing it) is what makes AUTOMOC generate its moc, the same way
# qcustomplot's target needs AUTOMOC set explicitly elsewhere in this project.
set_target_properties(Sessio_external_url_meeting_provider_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_external_url_meeting_provider_tests)

# LiveKit meeting provider stub tests
add_executable(Sessio_livekit_meeting_provider_tests livekit_meeting_provider_tests.cpp)
target_link_libraries(Sessio_livekit_meeting_provider_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_meeting
)
set_target_properties(Sessio_livekit_meeting_provider_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_livekit_meeting_provider_tests)

# Meeting coordinator tests
add_executable(Sessio_meeting_coordinator_tests
    meeting_coordinator_tests.cpp
    meeting_provider_test_listener.h
)
target_link_libraries(Sessio_meeting_coordinator_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_meeting
)
set_target_properties(Sessio_meeting_coordinator_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_meeting_coordinator_tests)

# Backup tests
find_package(reflectcpp CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
add_executable(Sessio_backup_tests
    backup_tests.cpp
    ../src/app/auto_backup_scheduler.cpp
    ../src/app/backup_encryption_policy.cpp
    ../src/app/credential_store.cpp
    ../src/app/app_lock_service.cpp
    ../src/app/app_lock_controller.cpp
)
target_link_libraries(Sessio_backup_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_config
    Sessio_database
    Sessio_backup
    Sessio_widgets
    Poco::Foundation
    Poco::Zip
    reflectcpp::reflectcpp
    unofficial-sodium::sodium
    qt6keychain
)
target_include_directories(Sessio_backup_tests PRIVATE
    ${qtkeychain_SOURCE_DIR}
    ${qtkeychain_BINARY_DIR}
)
gtest_discover_tests(Sessio_backup_tests)
