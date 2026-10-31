cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_webcall)

find_package(Qt6 REQUIRED COMPONENTS Core Network WebChannel WebEngineCore WebEngineWidgets)

qt_add_library(${TARGET_NAME} STATIC
        webcall_version.h
        webcall_version.cpp
        audio_chunk.h
        audio_chunk.cpp
        audio_bridge_server.h
        audio_bridge_server.cpp
        web_call_bridge.h
        web_call_bridge.cpp
        web_call_provider.h
        web_call_provider.cpp
        call_scheme_handler.h
        call_scheme_handler.cpp
)

set_target_properties(${TARGET_NAME} PROPERTIES AUTOMOC ON)

target_link_libraries(${TARGET_NAME} PUBLIC
        ${PROJECT_NAME}_video
        ${PROJECT_NAME}_meeting
        Qt6::Core
        Qt6::Network
        Qt6::WebChannel
        Qt6::WebEngineCore
        Qt6::WebEngineWidgets
)

target_include_directories(${TARGET_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# The call page is served to the WebEngine view from the binary:
# sessio-call://app/jitsi-call/<file> -> :/jitsi-call/<file>
set(JITSI_CALL_SRC ${CMAKE_SOURCE_DIR}/web/jitsi-call/src)
qt_add_resources(${TARGET_NAME} jitsi_call
        PREFIX /jitsi-call
        BASE ${JITSI_CALL_SRC}
        FILES
        ${JITSI_CALL_SRC}/index.html
        ${JITSI_CALL_SRC}/app.js
        ${JITSI_CALL_SRC}/chunk.js
        ${JITSI_CALL_SRC}/copy.js
        ${JITSI_CALL_SRC}/fake-adapter.js
        ${JITSI_CALL_SRC}/jitsi-adapter.js
        ${JITSI_CALL_SRC}/participants.js
        ${JITSI_CALL_SRC}/pcm-tap.js
        ${JITSI_CALL_SRC}/styles.css
)
