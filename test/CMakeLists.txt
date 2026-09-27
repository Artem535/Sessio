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

# AppRole tests
add_executable(Sessio_app_role_tests app_role_tests.cpp)
target_link_libraries(Sessio_app_role_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_config
)
gtest_discover_tests(Sessio_app_role_tests)

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

# Device manager tests
add_executable(Sessio_device_manager_tests
    ${CMAKE_SOURCE_DIR}/src/video/device_manager.cpp
    device_manager_tests.cpp
)
target_include_directories(Sessio_device_manager_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_device_manager_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Core
    Qt6::Multimedia
)
gtest_discover_tests(Sessio_device_manager_tests)

# Video capture adapter smoke test (own main(), not GoogleTest — needs a
# running Qt event loop and QThread lifecycle, doesn't fit gtest's own
# main()). Proves repeated start/stop/destroy cycles never hang, per this
# plan's Task 9 lifecycle-stress acceptance criterion.
add_executable(Sessio_video_capture_adapter_smoke_test
    ${CMAKE_SOURCE_DIR}/src/video/video_capture_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/video/video_capture_worker.cpp
    ${CMAKE_SOURCE_DIR}/src/video/frame_convert.cpp
    video_capture_adapter_smoke_test.cpp
)
target_include_directories(Sessio_video_capture_adapter_smoke_test PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_video_capture_adapter_smoke_test PRIVATE
    Qt6::Core
    Qt6::Multimedia
    LiveKit::livekit
)
add_test(NAME VideoCaptureAdapterSmokeTest COMMAND Sessio_video_capture_adapter_smoke_test)
set_tests_properties(VideoCaptureAdapterSmokeTest PROPERTIES TIMEOUT 30)

# Audio capture adapter smoke test (own main(), not GoogleTest — mirrors the
# video capture adapter smoke test above). Proves repeated start/destroy
# cycles on the GUI thread never hang, per this plan's Task 9 lifecycle-stress
# acceptance criterion.
add_executable(Sessio_audio_capture_adapter_smoke_test
    ${CMAKE_SOURCE_DIR}/src/video/audio_capture_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/video/audio_chunker.cpp
    audio_capture_adapter_smoke_test.cpp
)
target_include_directories(Sessio_audio_capture_adapter_smoke_test PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_audio_capture_adapter_smoke_test PRIVATE
    Qt6::Core
    Qt6::Multimedia
    LiveKit::livekit
)
add_test(NAME AudioCaptureAdapterSmokeTest COMMAND Sessio_audio_capture_adapter_smoke_test)
set_tests_properties(AudioCaptureAdapterSmokeTest PROPERTIES TIMEOUT 30)

# Remote video renderer smoke test (own main(), not GoogleTest — mirrors the
# local capture adapter smoke tests above). Proves repeated
# attach(null)/detach/destroy cycles never hang, which is what a
# Leave-before-any-remote-track-ever-subscribed race would exercise in
# production; no real remote LiveKit track is reachable without a server and
# a second participant.
add_executable(Sessio_remote_video_renderer_smoke_test
    ${CMAKE_SOURCE_DIR}/src/video/remote_video_renderer.cpp
    remote_video_renderer_smoke_test.cpp
)
target_include_directories(Sessio_remote_video_renderer_smoke_test PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_remote_video_renderer_smoke_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
    LiveKit::livekit
)
add_test(NAME RemoteVideoRendererSmokeTest COMMAND Sessio_remote_video_renderer_smoke_test)
set_tests_properties(RemoteVideoRendererSmokeTest PROPERTIES TIMEOUT 30)

# LiveKit video provider smoke test (own main(), not GoogleTest — mirrors the
# other video smoke tests above). Proves repeated construct/join-against-an-
# unreachable-url/destroy cycles never hang, and that join() against a real
# SDK call that will fail reports joinFailed() rather than hanging or
# crashing, per this plan's Task 9 lifecycle-stress acceptance criterion.
#
# video_provider.h defines the Q_OBJECT VideoProvider base class but has no
# paired .cpp of its own (CMake's AUTOMOC only auto-scans a header that
# shares a .cpp's basename); listing it explicitly as a source here — same
# pattern as meeting_provider_test_listener.h below — is what makes AUTOMOC
# generate its moc so VideoProvider's signals link.
add_executable(Sessio_livekit_video_provider_smoke_test
    ${CMAKE_SOURCE_DIR}/src/video/video_provider.h
    ${CMAKE_SOURCE_DIR}/src/video/livekit_video_provider.cpp
    ${CMAKE_SOURCE_DIR}/src/video/video_capture_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/video/video_capture_worker.cpp
    ${CMAKE_SOURCE_DIR}/src/video/audio_capture_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/video/audio_chunker.cpp
    ${CMAKE_SOURCE_DIR}/src/video/frame_convert.cpp
    ${CMAKE_SOURCE_DIR}/src/video/remote_video_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/video/remote_audio_player.cpp
    ${CMAKE_SOURCE_DIR}/src/video/device_manager.cpp
    livekit_video_provider_smoke_test.cpp
)
target_include_directories(Sessio_livekit_video_provider_smoke_test PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_livekit_video_provider_smoke_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::OpenGLWidgets
    LiveKit::livekit
)
set_target_properties(Sessio_livekit_video_provider_smoke_test PROPERTIES AUTOMOC ON)
add_test(NAME LiveKitVideoProviderSmokeTest COMMAND Sessio_livekit_video_provider_smoke_test)
set_tests_properties(LiveKitVideoProviderSmokeTest PROPERTIES TIMEOUT 40)

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

# LiveKit meeting provider tests (against a fake token-backend HTTP server,
# same fake used by Sessio_token_backend_client_tests)
find_package(Qt6 REQUIRED COMPONENTS Network Test)
add_executable(Sessio_livekit_meeting_provider_tests
    fake_token_backend_server.h
    livekit_meeting_provider_tests.cpp
)
target_link_libraries(Sessio_livekit_meeting_provider_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Network
    Qt6::Test
    Sessio_meeting
)
set_target_properties(Sessio_livekit_meeting_provider_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_livekit_meeting_provider_tests)

# Meeting coordinator tests
find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(Sessio_meeting_coordinator_tests
    meeting_coordinator_tests.cpp
    meeting_provider_test_listener.h
)
target_link_libraries(Sessio_meeting_coordinator_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Test
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

# Video session state machine tests
#
# video_provider.h defines the Q_OBJECT VideoProvider base class but has no
# paired .cpp of its own (CMake's AUTOMOC only auto-scans a header that
# shares a .cpp's basename); listing it explicitly as a source here — same
# pattern as the LiveKit video provider smoke test above — is what makes
# AUTOMOC generate its moc so VideoProvider's signals link.
find_package(Qt6 REQUIRED COMPONENTS Test StateMachine)
add_executable(Sessio_video_session_tests
    ${CMAKE_SOURCE_DIR}/src/video/video_provider.h
    ${CMAKE_SOURCE_DIR}/src/video/video_session.cpp
    video_session_tests.cpp
)
target_include_directories(Sessio_video_session_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_video_session_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Core
    Qt6::Test
    Qt6::StateMachine
)
set_target_properties(Sessio_video_session_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_video_session_tests)

# Role selection dialog tests
find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(Sessio_role_selection_dialog_tests role_selection_dialog_tests.cpp)
target_include_directories(Sessio_role_selection_dialog_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/app)
target_link_libraries(Sessio_role_selection_dialog_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Widgets
    Qt6::Test
    Sessio_app
)
set_target_properties(Sessio_role_selection_dialog_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_role_selection_dialog_tests)

# Token response parser tests
add_executable(Sessio_token_response_parser_tests token_response_parser_tests.cpp)
target_link_libraries(Sessio_token_response_parser_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_token_client
)
gtest_discover_tests(Sessio_token_response_parser_tests)

# Token backend client tests
find_package(Qt6 REQUIRED COMPONENTS Network Test)
add_executable(Sessio_token_backend_client_tests
    fake_token_backend_server.h
    token_backend_client_tests.cpp
)
target_link_libraries(Sessio_token_backend_client_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Network
    Qt6::Test
    Sessio_token_client
)
set_target_properties(Sessio_token_backend_client_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_token_backend_client_tests)

# Settings dialog LiveKit section tests
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
add_executable(Sessio_settings_dialog_livekit_section_tests
    fake_token_backend_credential_store.h
    settings_dialog_livekit_section_tests.cpp
)
target_include_directories(Sessio_settings_dialog_livekit_section_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/app)
target_link_libraries(Sessio_settings_dialog_livekit_section_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Widgets
    Qt6::Test
    Sessio_app
)
set_target_properties(Sessio_settings_dialog_livekit_section_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_settings_dialog_livekit_section_tests)

# CallEntryWidget tests
add_executable(Sessio_call_entry_widget_tests call_entry_widget_tests.cpp)
target_link_libraries(Sessio_call_entry_widget_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Widgets
    Qt6::Test
    Sessio_calls_page
)
set_target_properties(Sessio_call_entry_widget_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_call_entry_widget_tests)

# ClientModeWindow structural tests
add_test(NAME ClientModeWindowHeaderHasNoDatabaseDependency
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/client_mode_window_structural_test.sh
                 ${CMAKE_SOURCE_DIR}/src/app/client_mode_window.h)
add_test(NAME ClientModeWindowSourceHasNoDatabaseDependency
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/client_mode_window_structural_test.sh
                 ${CMAKE_SOURCE_DIR}/src/app/client_mode_window.cpp)
