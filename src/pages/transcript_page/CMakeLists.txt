set(TARGET_NAME ${PROJECT_NAME}_transcription_ui)
qt_add_library(${TARGET_NAME} STATIC
    transcription_settings_panel.h transcription_settings_panel.cpp
    transcript_page.h transcript_page.cpp
    call_transcription_controller.h call_transcription_controller.cpp
    call_side_panel.h call_side_panel.cpp
    transcript_panel.h transcript_panel.cpp
    transcription_consent_dialog.h transcription_consent_dialog.cpp
    failure_text.h failure_text.cpp)
set_target_properties(${TARGET_NAME} PROPERTIES AUTOMOC ON)
target_include_directories(${TARGET_NAME} PRIVATE ${qlementine_SOURCE_DIR}/lib/include)
target_include_directories(${TARGET_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${TARGET_NAME} PUBLIC Qt6::Widgets qlementine
    ${PROJECT_NAME}_database ${PROJECT_NAME}_widgets ${PROJECT_NAME}_call_transcription)
