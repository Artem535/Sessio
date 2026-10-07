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

# Transcript data layer tests
add_executable(Sessio_database_transcript_tests database_transcript_tests.cpp)
target_link_libraries(Sessio_database_transcript_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_config
    Sessio_database
    Poco::Foundation
)
gtest_discover_tests(Sessio_database_transcript_tests)

# Schedule identity / transactional outbox tests
add_executable(Sessio_schedule_outbox_tests schedule_outbox_tests.cpp)
target_link_libraries(Sessio_schedule_outbox_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_config
    Sessio_database
    Poco::Foundation
)
gtest_discover_tests(Sessio_schedule_outbox_tests)

# Schedule snapshot export / wire tests
add_executable(Sessio_schedule_snapshot_tests schedule_snapshot_tests.cpp)
target_link_libraries(Sessio_schedule_snapshot_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_meeting
    Sessio_database
)
gtest_discover_tests(Sessio_schedule_snapshot_tests)

# Schedule sync service tests (real DB + fake token-backend over local HTTP)
find_package(Qt6 REQUIRED COMPONENTS Network Test)
add_executable(Sessio_schedule_sync_tests
    fake_schedule_backend.h
    schedule_sync_tests.cpp
)
target_link_libraries(Sessio_schedule_sync_tests PRIVATE
    GTest::gtest
    Qt6::Network
    Qt6::Test
    Sessio_config
    Sessio_database
    Sessio_meeting
    Sessio_token_client
    Poco::Foundation
)
set_target_properties(Sessio_schedule_sync_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_schedule_sync_tests)

# Zoneinfo resolution for packaged builds (and the schedule engine's use of it)
add_executable(Sessio_schedule_zoneinfo_tests schedule_zoneinfo_tests.cpp)
target_link_libraries(Sessio_schedule_zoneinfo_tests PRIVATE
    GTest::gtest
    Sessio_meeting
)
gtest_discover_tests(Sessio_schedule_zoneinfo_tests)

# Series invitation service / join target tests (real DB + fake backend model)
add_executable(Sessio_series_invitation_tests
    fake_schedule_backend.h
    series_backend_model.h
    fake_series_invitation_store.h
    series_invitation_service_tests.cpp
)
target_link_libraries(Sessio_series_invitation_tests PRIVATE
    GTest::gtest
    Qt6::Network
    Qt6::Test
    Sessio_config
    Sessio_database
    Sessio_meeting
    Sessio_token_client
    Poco::Foundation
)
set_target_properties(Sessio_series_invitation_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_series_invitation_tests)

# Adapter from the keychain credential store to the schedule services
add_executable(Sessio_store_credential_reader_tests
    fake_token_backend_credential_store.h
    store_credential_reader_tests.cpp
    ../src/app/store_credential_reader.cpp
    ../src/app/store_credential_reader.h
    ../src/app/token_backend_credential_store.h
    ../src/app/token_backend_credential_store.cpp
)
target_include_directories(Sessio_store_credential_reader_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app
    ${qtkeychain_SOURCE_DIR}
    ${qtkeychain_BINARY_DIR}
)
target_link_libraries(Sessio_store_credential_reader_tests PRIVATE
    GTest::gtest
    Qt6::Network
    Qt6::Test
    Sessio_meeting
    qt6keychain
)
set_target_properties(Sessio_store_credential_reader_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_store_credential_reader_tests)

# Timeline model + published series (real local DB, no network)
find_package(Qt6 REQUIRED COMPONENTS Network Test)
add_executable(Sessio_timeline_series_tests
    fake_schedule_backend.h
    timeline_series_tests.cpp
)
target_link_libraries(Sessio_timeline_series_tests PRIVATE
    GTest::gtest
    Qt6::Network
    Qt6::Test
    Sessio_config
    Sessio_database
    Sessio_event_view
    Sessio_meeting
    Poco::Foundation
)
set_target_properties(Sessio_timeline_series_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_timeline_series_tests)

# Month projection and the actual calendar widget, with a real isolated DB.
add_executable(Sessio_month_calendar_tests
    month_calendar_tests.cpp
    ../src/pages/event_info_page/month_calendar_widget.cpp
    ../src/pages/event_info_page/month_calendar_widget.h
)
target_include_directories(Sessio_month_calendar_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/pages/event_info_page
)
target_link_libraries(Sessio_month_calendar_tests PRIVATE
    GTest::gtest Qt6::Widgets Qt6::Test Sessio_event_view
)
set_target_properties(Sessio_month_calendar_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_month_calendar_tests)

# Recurring-call facade: publication, invitation, join targets, migration
add_executable(Sessio_series_call_tests
    fake_schedule_backend.h
    series_backend_model.h
    fake_series_invitation_store.h
    series_call_service_tests.cpp
)
target_link_libraries(Sessio_series_call_tests PRIVATE
    GTest::gtest
    Qt6::Network
    Qt6::Test
    Sessio_config
    Sessio_database
    Sessio_event_view
    Sessio_meeting
    Sessio_token_client
    Poco::Foundation
)
set_target_properties(Sessio_series_call_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_series_call_tests)

# Status text of a published recurring call series
add_executable(Sessio_series_call_status_text_tests
    series_call_status_text_tests.cpp
)
target_include_directories(Sessio_series_call_status_text_tests PRIVATE
    ${qlementine_SOURCE_DIR}/lib/include
)
target_link_libraries(Sessio_series_call_status_text_tests PRIVATE
    GTest::gtest
    Qt6::Widgets
    Sessio_event_page
    Sessio_meeting
)
gtest_discover_tests(Sessio_series_call_status_text_tests)

# Event editor with published recurring call series (real DB + backend model)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Test)
add_executable(Sessio_series_call_ui_tests
    fake_schedule_backend.h
    series_backend_model.h
    fake_series_invitation_store.h
    series_call_fixture.h
    series_call_ui_tests.cpp
)
target_include_directories(Sessio_series_call_ui_tests PRIVATE
    ${qlementine_SOURCE_DIR}/lib/include
)
target_link_libraries(Sessio_series_call_ui_tests PRIVATE
    GTest::gtest
    Qt6::Widgets
    Qt6::Network
    Qt6::Test
    Sessio_config
    Sessio_database
    Sessio_event_page
    Sessio_event_view
    Sessio_meeting
    Sessio_token_client
    Poco::Foundation
)
set_target_properties(Sessio_series_call_ui_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_series_call_ui_tests)

# Timezone confirmation dialog for publishing a series
add_executable(Sessio_series_timezone_dialog_tests
    series_timezone_dialog_tests.cpp
)
target_include_directories(Sessio_series_timezone_dialog_tests PRIVATE
    ${qlementine_SOURCE_DIR}/lib/include
)
target_link_libraries(Sessio_series_timezone_dialog_tests PRIVATE
    GTest::gtest
    Qt6::Widgets
    Sessio_event_page
)
gtest_discover_tests(Sessio_series_timezone_dialog_tests)

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
find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(Sessio_device_manager_tests
    ${CMAKE_SOURCE_DIR}/src/video/device_manager.h
    ${CMAKE_SOURCE_DIR}/src/video/device_manager.cpp
    device_manager_tests.cpp
)
target_include_directories(Sessio_device_manager_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_device_manager_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Core
    Qt6::Multimedia
    Qt6::Test
)
set_target_properties(Sessio_device_manager_tests PROPERTIES AUTOMOC ON)
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
# local capture adapter smoke tests above). Exercises Qt-only source attachment,
# pending frame notifications and source deletion while a renderer survives.
# No SDK is linked; this is a lifecycle test, not hardware GL paint verification.
add_executable(Sessio_remote_video_renderer_smoke_test
    ${CMAKE_SOURCE_DIR}/src/video/video_frame_source.cpp
    ${CMAKE_SOURCE_DIR}/src/video/remote_video_renderer.cpp
    remote_video_renderer_smoke_test.cpp
)
target_include_directories(Sessio_remote_video_renderer_smoke_test PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_remote_video_renderer_smoke_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
)
add_test(NAME RemoteVideoRendererSmokeTest COMMAND Sessio_remote_video_renderer_smoke_test)
set_tests_properties(RemoteVideoRendererSmokeTest PROPERTIES TIMEOUT 30)

# Remote video renderer letterbox geometry tests (plain GoogleTest, no GL
# context needed — exercises only the pure scaledFrameRect() helper that
# paintGL() uses to place a KeepAspectRatio-scaled frame, per this plan's
# Task 4 transparency-artifact fix).
add_executable(Sessio_remote_video_renderer_letterbox_test
    ${CMAKE_SOURCE_DIR}/src/video/video_frame_source.cpp
    ${CMAKE_SOURCE_DIR}/src/video/remote_video_renderer.cpp
    remote_video_renderer_letterbox_test.cpp
)
target_include_directories(Sessio_remote_video_renderer_letterbox_test PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_remote_video_renderer_letterbox_test PRIVATE
    GTest::gtest
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
)
add_test(NAME RemoteVideoRendererLetterboxTest COMMAND Sessio_remote_video_renderer_letterbox_test)

# LiveKit video provider smoke test (own main(), not GoogleTest — mirrors the
# other video smoke tests above). Exercises copied participant/track handlers,
# generation/SID guards, payload release, zero QWidget construction, and five
# construct/refused-join/destroy cycles. Real room media is a separate gate.
#
# video_provider.h defines the Q_OBJECT VideoProvider base class but has no
# paired .cpp of its own (CMake's AUTOMOC only auto-scans a header that
# shares a .cpp's basename); listing it explicitly as a source here — same
# pattern as meeting_provider_test_listener.h below — is what makes AUTOMOC
# generate its moc so VideoProvider's signals link.
add_executable(Sessio_livekit_video_provider_smoke_test
    ${CMAKE_SOURCE_DIR}/src/video/video_provider.h
    ${CMAKE_SOURCE_DIR}/src/video/participant_model.cpp
    ${CMAKE_SOURCE_DIR}/src/video/video_frame_source.cpp
    ${CMAKE_SOURCE_DIR}/src/video/livekit_video_provider.cpp
    ${CMAKE_SOURCE_DIR}/src/video/livekit_video_frame_source.cpp
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

add_executable(Sessio_remote_audio_player_lifecycle_test
    ${CMAKE_SOURCE_DIR}/src/video/remote_audio_player.cpp
    remote_audio_player_lifecycle_test.cpp
)
target_include_directories(Sessio_remote_audio_player_lifecycle_test PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_remote_audio_player_lifecycle_test PRIVATE Qt6::Core Qt6::Multimedia LiveKit::livekit)
add_test(NAME RemoteAudioPlayerLifecycleTest COMMAND Sessio_remote_audio_player_lifecycle_test)

add_executable(Sessio_livekit_video_frame_source_smoke_test
    ${CMAKE_SOURCE_DIR}/src/video/video_frame_source.cpp
    ${CMAKE_SOURCE_DIR}/src/video/livekit_video_frame_source.cpp
    livekit_video_frame_source_smoke_test.cpp
)
target_include_directories(Sessio_livekit_video_frame_source_smoke_test PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_livekit_video_frame_source_smoke_test PRIVATE Qt6::Core Qt6::Gui LiveKit::livekit)
add_test(NAME LiveKitVideoFrameSourceSmokeTest COMMAND Sessio_livekit_video_frame_source_smoke_test)
set_tests_properties(LiveKitVideoFrameSourceSmokeTest PROPERTIES TIMEOUT 15)

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

# Meeting coordinator tests (also exercised against a fake token-backend
# HTTP server, same double as Sessio_livekit_meeting_provider_tests, to prove
# setTokenBackendBaseUrl()/setBearerCredential() retarget the LiveKit
# provider it holds — fixwave group 5, bug 3)
find_package(Qt6 REQUIRED COMPONENTS Network Test)
add_executable(Sessio_meeting_coordinator_tests
    fake_token_backend_server.h
    meeting_coordinator_tests.cpp
    meeting_provider_test_listener.h
)
target_link_libraries(Sessio_meeting_coordinator_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Network
    Qt6::Test
    Sessio_meeting
)
set_target_properties(Sessio_meeting_coordinator_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_meeting_coordinator_tests)

# Calendar page layout tests (Day/Month switch, shared inspector)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Test)
add_executable(Sessio_calendar_layout_tests calendar_layout_tests.cpp)
target_link_libraries(Sessio_calendar_layout_tests PRIVATE
    GTest::gtest Qt6::Widgets Qt6::Test Sessio_event_page)
gtest_discover_tests(Sessio_calendar_layout_tests)

# QEventDetailsWidget meeting tests (real MeetingCoordinator against the fake
# token-backend HTTP server, same double as Sessio_livekit_meeting_provider_tests)
add_executable(Sessio_qevent_details_widget_tests
    fake_token_backend_server.h
    qevent_details_widget_tests.cpp
)
target_include_directories(Sessio_qevent_details_widget_tests PRIVATE
    ${qlementine_SOURCE_DIR}/lib/include
)
target_link_libraries(Sessio_qevent_details_widget_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Widgets
    Qt6::Network
    Qt6::Test
    Sessio_event_page
    Sessio_meeting
)
set_target_properties(Sessio_qevent_details_widget_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_qevent_details_widget_tests)

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
#
# Qt6::Multimedia is required here (not just Core/Test/StateMachine) because
# video_provider.h itself now includes <QAudioDevice>/<QCameraDevice> for the
# switchCamera()/switchMicrophone()/switchSpeaker() device-switch API; unlike
# the targets below that link the Sessio_video library (which already
# PUBLIC-links Qt6::Multimedia), this target compiles video_provider.h
# directly and needs the dependency named explicitly.
find_package(Qt6 REQUIRED COMPONENTS Test StateMachine Multimedia)
add_executable(Sessio_participant_model_tests
    ${CMAKE_SOURCE_DIR}/src/video/participant_model.cpp
    participant_model_tests.cpp
)
target_include_directories(Sessio_participant_model_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_participant_model_tests PRIVATE GTest::gtest_main Qt6::Core Qt6::Test)
set_target_properties(Sessio_participant_model_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_participant_model_tests)

add_executable(Sessio_video_frame_source_tests
    ${CMAKE_SOURCE_DIR}/src/video/video_frame_source.cpp
    video_frame_source_tests.cpp
)
target_include_directories(Sessio_video_frame_source_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_video_frame_source_tests PRIVATE GTest::gtest_main Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(Sessio_video_frame_source_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_video_frame_source_tests)

add_executable(Sessio_video_session_tests
    ${CMAKE_SOURCE_DIR}/src/video/video_provider.h
    ${CMAKE_SOURCE_DIR}/src/video/participant_model.cpp
    ${CMAKE_SOURCE_DIR}/src/video/video_frame_source.cpp
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
    Qt6::Multimedia
)
set_target_properties(Sessio_video_session_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_video_session_tests)

# FakeVideoProvider tests
add_executable(Sessio_fake_video_provider_tests
    fake_video_provider_tests.cpp
)
target_link_libraries(Sessio_fake_video_provider_tests PRIVATE
    GTest::gtest
    Qt6::Widgets
    Sessio_video
)
set_target_properties(Sessio_fake_video_provider_tests PROPERTIES AUTOMOC ON)
add_test(NAME FakeVideoProviderTests COMMAND Sessio_fake_video_provider_tests)

# Audio tap tests (header-only audio_sink.h)
add_executable(Sessio_audio_tap_tests audio_tap_tests.cpp)
target_include_directories(Sessio_audio_tap_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
target_link_libraries(Sessio_audio_tap_tests PRIVATE GTest::gtest GTest::gtest_main Qt6::Core)
gtest_discover_tests(Sessio_audio_tap_tests)

if(SESSIO_TRANSCRIPTION_ENABLED)
add_executable(Sessio_call_transcription_controller_tests call_transcription_controller_tests.cpp)
target_include_directories(Sessio_call_transcription_controller_tests PRIVATE ${CMAKE_SOURCE_DIR}/test ${CMAKE_SOURCE_DIR}/test/transcription)
target_link_libraries(Sessio_call_transcription_controller_tests PRIVATE GTest::gtest Qt6::Widgets Qt6::Test Sessio_transcription_ui Sessio_video)
gtest_discover_tests(Sessio_call_transcription_controller_tests)
add_executable(Sessio_transcript_panel_tests transcript_panel_tests.cpp)
target_link_libraries(Sessio_transcript_panel_tests PRIVATE
    GTest::gtest Qt6::Widgets Qt6::Test Sessio_transcription_ui)
gtest_discover_tests(Sessio_transcript_panel_tests)
# Phrase writer tests (Qt-free writer thread)
add_executable(Sessio_phrase_writer_tests phrase_writer_tests.cpp)
target_link_libraries(Sessio_phrase_writer_tests PRIVATE
    GTest::gtest GTest::gtest_main Sessio_call_transcription)
gtest_discover_tests(Sessio_phrase_writer_tests)

# Production engine factory tests (no model files needed)
add_executable(Sessio_engine_factory_tests engine_factory_tests.cpp)
target_link_libraries(Sessio_engine_factory_tests PRIVATE
    GTest::gtest GTest::gtest_main Sessio_call_transcription)
gtest_discover_tests(Sessio_engine_factory_tests)

# Call transcription session tests (QApplication main, fakes for engine and provider)
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
add_executable(Sessio_transcription_session_tests transcription_session_tests.cpp)
target_include_directories(Sessio_transcription_session_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/test
    ${CMAKE_SOURCE_DIR}/test/transcription)
target_link_libraries(Sessio_transcription_session_tests PRIVATE
    GTest::gtest
    Qt6::Widgets
    Qt6::Test
    Sessio_call_transcription
    Sessio_transcription
    Sessio_video)
set_target_properties(Sessio_transcription_session_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_transcription_session_tests)
endif()

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
    fake_schedule_backend.h
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

# Settings dialog tab layout tests
add_executable(Sessio_settings_dialog_layout_tests
    fake_token_backend_credential_store.h
    settings_dialog_layout_tests.cpp
)
target_include_directories(Sessio_settings_dialog_layout_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/app)
target_link_libraries(Sessio_settings_dialog_layout_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Widgets
    Qt6::Test
    Sessio_app
)
set_target_properties(Sessio_settings_dialog_layout_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_settings_dialog_layout_tests)

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

# Audio-check helpers (mic level, test tone)
add_executable(Sessio_audio_check_tests audio_check_tests.cpp)
target_link_libraries(Sessio_audio_check_tests PRIVATE
    GTest::gtest
    Qt6::Widgets
    Qt6::Multimedia
    Sessio_calls_page
)
gtest_discover_tests(Sessio_audio_check_tests)

# DeviceCheckWidget tests
add_executable(Sessio_device_check_widget_tests device_check_widget_tests.cpp)
target_link_libraries(Sessio_device_check_widget_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Widgets
    Qt6::Test
    Sessio_calls_page
    Sessio_video
)
set_target_properties(Sessio_device_check_widget_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_device_check_widget_tests)

# CallPage tests
#
# Unlike Sessio_video_session_tests (which compiles video_session.cpp/
# video_provider.h directly and does NOT link Sessio_video), this target
# links the Sessio_video library, which already contains VideoProvider's
# compiled moc. Listing video_provider.h as an extra AUTOMOC source here (as
# Sessio_video_session_tests does) would generate a second definition of
# VideoProvider's moc symbols and fail to link with "multiple definition of
# `pcm::video::VideoProvider::staticMetaObject'" — so this target follows the
# Sessio_device_check_widget_tests pattern instead: link Sessio_video and let
# it supply the moc.
find_package(Qt6 REQUIRED COMPONENTS StateMachine Test)
add_executable(Sessio_call_page_tests
    call_page_tests.cpp
    call_layout_strategy_tests.cpp
)
target_link_libraries(Sessio_call_page_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Widgets
    Qt6::Test
    Qt6::StateMachine
    Sessio_calls_page
    Sessio_video
    Sessio_widgets
)
target_include_directories(Sessio_call_page_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/widgets)
set_target_properties(Sessio_call_page_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_call_page_tests)

# Explicit manual integration gate: needs a disposable loopback LiveKit server
# (--room) or a native display with hardware OpenGL (--native-ui). It is not a
# default CTest entry and never contacts a production room.
add_executable(Sessio_group_call_runtime_smoke group_call_runtime_smoke.cpp)
target_link_libraries(Sessio_group_call_runtime_smoke PRIVATE
    Sessio_calls_page Sessio_video Qt6::Widgets Qt6::OpenGLWidgets LiveKit::livekit
)

# Manual tool: joins a call as fake clients publishing an animation and a tone, so a
# specialist can be tested from one device. Talks to a real token backend; not a CTest entry.
add_executable(Sessio_fake_client fake_client.cpp)
target_link_libraries(Sessio_fake_client PRIVATE
    Qt6::Gui Qt6::Network Sessio_token_client LiveKit::livekit
)

# CallsPage tests
#
# Like Sessio_call_page_tests above, this links Sessio_calls_page (which
# already links Sessio_video, which already contains VideoProvider's compiled
# moc). The brief's illustrative CMake also listed video_provider.h as an
# extra AUTOMOC source, but doing that here would generate a second
# definition of VideoProvider's moc symbols and fail to link with "multiple
# definition of `pcm::video::VideoProvider::staticMetaObject'" — the exact
# failure mode Task 12 already hit and documented above. So that line is
# deliberately omitted; Sessio_video supplies the moc instead.
find_package(Qt6 REQUIRED COMPONENTS StateMachine Network Test)
add_executable(Sessio_calls_page_tests
    fake_token_backend_server.h
    calls_page_tests.cpp
)
target_link_libraries(Sessio_calls_page_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Widgets
    Qt6::StateMachine
    Qt6::Network
    Qt6::Test
    Sessio_calls_page
    Sessio_token_client
    Sessio_video
)
set_target_properties(Sessio_calls_page_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_calls_page_tests)

# MainWindow Calls tab tests
find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_executable(Sessio_main_window_calls_tab_tests main_window_calls_tab_tests.cpp)
target_link_libraries(Sessio_main_window_calls_tab_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Widgets
    Sessio_app
    Sessio_token_client
)
gtest_discover_tests(Sessio_main_window_calls_tab_tests)

# ClientModeWindow tests
find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_executable(Sessio_client_mode_window_tests client_mode_window_tests.cpp)
target_link_libraries(Sessio_client_mode_window_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Widgets
    Sessio_app
    Sessio_token_client
)
gtest_discover_tests(Sessio_client_mode_window_tests)

# Sessio URL parsing tests
add_executable(Sessio_sessio_url_tests sessio_url_tests.cpp)
target_link_libraries(Sessio_sessio_url_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_app
)
gtest_discover_tests(Sessio_sessio_url_tests)

# Single-instance guard tests
find_package(Qt6 REQUIRED COMPONENTS Network Test)
add_executable(Sessio_single_instance_guard_tests single_instance_guard_tests.cpp)
target_link_libraries(Sessio_single_instance_guard_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Network
    Qt6::Test
    Sessio_app
)
set_target_properties(Sessio_single_instance_guard_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(Sessio_single_instance_guard_tests)

# App role switch tests (switcher, restarter, confirmation prompt)
add_executable(Sessio_app_role_switcher_tests app_role_switcher_tests.cpp)
target_include_directories(Sessio_app_role_switcher_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/app)
target_link_libraries(Sessio_app_role_switcher_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Widgets
    Qt6::Network
    Sessio_app
)
gtest_discover_tests(Sessio_app_role_switcher_tests)

# ClientModeWindow structural tests
add_test(NAME ClientModeWindowHeaderHasNoDatabaseDependency
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/client_mode_window_structural_test.sh
                 ${CMAKE_SOURCE_DIR}/src/app/client_mode_window.h)
add_test(NAME ClientModeWindowSourceHasNoDatabaseDependency
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/client_mode_window_structural_test.sh
                 ${CMAKE_SOURCE_DIR}/src/app/client_mode_window.cpp)
add_test(NAME ClientModeSettingsDialogHeaderHasNoDatabaseDependency
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/client_mode_window_structural_test.sh
                 ${CMAKE_SOURCE_DIR}/src/app/client_mode_settings_dialog.h)
add_test(NAME ClientModeSettingsDialogSourceHasNoDatabaseDependency
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/client_mode_window_structural_test.sh
                 ${CMAKE_SOURCE_DIR}/src/app/client_mode_settings_dialog.cpp)

# BusySpinner tests
add_executable(Sessio_busy_spinner_tests
    busy_spinner_tests.cpp
)
target_include_directories(Sessio_busy_spinner_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/widgets)
target_link_libraries(Sessio_busy_spinner_tests PRIVATE
    GTest::gtest
    Qt6::Widgets
    Sessio_widgets
)
set_target_properties(Sessio_busy_spinner_tests PROPERTIES AUTOMOC ON)
add_test(NAME BusySpinnerTests COMMAND Sessio_busy_spinner_tests)

# Call control icons tests (self-drawn glyph icons for the call control bar)
add_executable(Sessio_call_control_icons_tests
    call_control_icons_tests.cpp
)
target_include_directories(Sessio_call_control_icons_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/widgets)
target_link_libraries(Sessio_call_control_icons_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Qt6::Gui
    Sessio_widgets
)
gtest_discover_tests(Sessio_call_control_icons_tests)

# CPack runs install scripts through a DESTDIR staging tree. Keep the
# LiveKit-runtime bundler's staging contract executable without rebuilding
# the application or its vcpkg dependencies.
add_test(NAME BundleLiveKitLinuxDepsUsesDestdir
         COMMAND ${CMAKE_COMMAND}
                 -DPCM_SOURCE_DIR=${CMAKE_SOURCE_DIR}
                 -DPCM_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/bundle_livekit_linux_deps_test.cmake)

# Accent colour: theme values, helper, and the widgets that must derive from it
find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_executable(Sessio_accent_color_tests accent_color_tests.cpp)
target_include_directories(Sessio_accent_color_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/widgets
    ${CMAKE_SOURCE_DIR}/src/pages/event_info_page
)
target_compile_definitions(Sessio_accent_color_tests PRIVATE
    SESSIO_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(Sessio_accent_color_tests PRIVATE
    GTest::gtest Qt6::Widgets Sessio_event_page)
gtest_discover_tests(Sessio_accent_color_tests)

if(SESSIO_TRANSCRIPTION_ENABLED)
add_executable(Sessio_transcription_consent_dialog_tests transcription_consent_dialog_tests.cpp)
add_executable(Sessio_transcription_settings_panel_tests transcription_settings_panel_tests.cpp)
target_link_libraries(Sessio_transcription_settings_panel_tests PRIVATE
    GTest::gtest Qt6::Test Sessio_transcription_ui)
gtest_discover_tests(Sessio_transcription_settings_panel_tests)
add_executable(Sessio_call_side_panel_tests call_side_panel_tests.cpp)
target_include_directories(Sessio_call_side_panel_tests PRIVATE
    ${qlementine_SOURCE_DIR}/lib/include)
target_link_libraries(Sessio_call_side_panel_tests PRIVATE
    GTest::gtest Qt6::Widgets Qt6::Test Sessio_transcription_ui)
gtest_discover_tests(Sessio_call_side_panel_tests)
target_include_directories(Sessio_transcription_consent_dialog_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/pages/transcript_page)
target_link_libraries(Sessio_transcription_consent_dialog_tests PRIVATE
    GTest::gtest Qt6::Widgets Qt6::Test Sessio_transcription_ui)
gtest_discover_tests(Sessio_transcription_consent_dialog_tests)

add_executable(Sessio_transcript_page_tests transcript_page_tests.cpp)
target_link_libraries(Sessio_transcript_page_tests PRIVATE
    GTest::gtest Qt6::Test Sessio_transcription_ui)
gtest_discover_tests(Sessio_transcript_page_tests)

add_executable(Sessio_failure_text_tests failure_text_tests.cpp)
target_include_directories(Sessio_failure_text_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/pages/transcript_page)
target_link_libraries(Sessio_failure_text_tests PRIVATE
    GTest::gtest GTest::gtest_main Qt6::Core Sessio_transcription_ui)
gtest_discover_tests(Sessio_failure_text_tests)

# Call event resolver tests (materialising a recurring occurrence)
add_executable(Sessio_call_event_resolver_tests call_event_resolver_tests.cpp)
target_link_libraries(Sessio_call_event_resolver_tests PRIVATE
    GTest::gtest GTest::gtest_main Sessio_call_transcription Sessio_event_view
    Sessio_database Sessio_config Poco::Foundation)
gtest_discover_tests(Sessio_call_event_resolver_tests)
endif()
