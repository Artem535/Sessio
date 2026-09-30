cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_video)

find_package(Qt6 REQUIRED COMPONENTS Core Multimedia MultimediaWidgets OpenGLWidgets StateMachine)

qt_add_library(${TARGET_NAME} STATIC
        video_provider.h
        participant_model.h
        participant_model.cpp
        video_frame_source.h
        video_frame_source.cpp
        livekit_video_frame_source.h
        livekit_video_frame_source.cpp
        video_session_state.h
        video_session.h
        video_session.cpp
        audio_chunker.h
        audio_chunker.cpp
        frame_convert.h
        frame_convert.cpp
        device_manager.h
        device_manager.cpp
        video_capture_worker.h
        video_capture_worker.cpp
        video_capture_adapter.h
        video_capture_adapter.cpp
        audio_capture_adapter.h
        audio_capture_adapter.cpp
        remote_video_renderer.h
        remote_video_renderer.cpp
        remote_audio_player.h
        remote_audio_player.cpp
        livekit_video_provider.h
        livekit_video_provider.cpp
)

target_link_libraries(${TARGET_NAME} PUBLIC
        Qt6::Core
        Qt6::Multimedia
        Qt6::MultimediaWidgets
        Qt6::OpenGLWidgets
        Qt6::StateMachine
        LiveKit::livekit
)

target_include_directories(${TARGET_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

get_filename_component(_lk_cmake_dir "${LiveKit_DIR}" DIRECTORY)
get_filename_component(_lk_lib_dir "${_lk_cmake_dir}" DIRECTORY)
target_link_directories(${TARGET_NAME} PUBLIC "${_lk_lib_dir}")

if(UNIX AND NOT APPLE)
  # The prebuilt Linux LiveKit SDK has a runtime dependency on libcurl, but
  # its exported CMake target omits that dependency. Link it explicitly so
  # GNU ld verifies the curl ABI required by liblivekit at build time.
  set(LIVEKIT_CURL_LIBRARY "" CACHE FILEPATH
      "libcurl shared library compatible with the prebuilt LiveKit Linux SDK")
  if(NOT LIVEKIT_CURL_LIBRARY)
    find_library(LIVEKIT_CURL_LIBRARY NAMES curl libcurl.so.4 REQUIRED)
  endif()
  target_link_libraries(${TARGET_NAME} PUBLIC "${LIVEKIT_CURL_LIBRARY}")
endif()

if(WIN32)
  get_filename_component(_lk_prefix "${_lk_lib_dir}" DIRECTORY)
  set(_lk_bin_dir "${_lk_prefix}/bin")
  foreach(_lk_runtime_dll IN ITEMS livekit.dll livekit_ffi.dll)
    if(EXISTS "${_lk_bin_dir}/${_lk_runtime_dll}")
      # Copied next to the Sessio EXECUTABLE (not this static library's own
      # .lib output directory, which is a different path and useless here —
      # Windows resolves a DLL relative to the .exe that needs it at
      # runtime, not relative to any static library linked into it) so a
      # local dev build can actually run without a missing-DLL error.
      # ${TARGET_NAME} (Sessio_video) is a static library with no DLLs of
      # its own to place; Sessio is this project's one executable target,
      # defined later in the top-level CMakeLists.txt — a forward reference
      # through a generator expression is fine, since it resolves after the
      # whole project is configured, not at the point this command is added.
      add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_lk_bin_dir}/${_lk_runtime_dll}"
                "$<TARGET_FILE_DIR:Sessio>")
    endif()
  endforeach()
endif()
