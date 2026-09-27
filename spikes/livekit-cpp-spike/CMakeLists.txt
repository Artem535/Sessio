# Throwaway spike for issue #77 — see docs/superpowers/specs/2026-09-25-livekit-cpp-spike-design.md
# Not part of the main PsyClientManager build; never add_subdirectory'd from the top-level CMakeLists.txt.
cmake_minimum_required(VERSION 3.28)
project(livekit_cpp_spike LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(LIVEKIT_SDK_VERSION "1.12.0" CACHE STRING "LiveKit C++ SDK version")

include(LiveKitSDK)
livekit_sdk_setup(
  VERSION "${LIVEKIT_SDK_VERSION}"
  SDK_DIR "${CMAKE_BINARY_DIR}/_deps/livekit-sdk"
)
find_package(LiveKit CONFIG REQUIRED)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets OpenGLWidgets)
qt_standard_project_setup()

qt_add_executable(livekit_spike
  src/main.cpp
  src/audio_chunker.cpp
  src/frame_convert.cpp
  src/video_capture_adapter.cpp
  src/video_capture_worker.cpp
  src/audio_capture_adapter.cpp
  src/remote_video_renderer.cpp
  src/remote_audio_player.cpp
  src/spike_window.cpp
)

target_link_libraries(livekit_spike PRIVATE
  LiveKit::livekit
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Multimedia
  Qt6::MultimediaWidgets
  Qt6::OpenGLWidgets
)

# Make -llivekit_ffi resolvable, matching the upstream example's own pattern
# (spikes/livekit-cpp-spike/cmake/LiveKitSDK.cmake only wires CMAKE_PREFIX_PATH
# for find_package; the actual FFI shared lib lives one directory up from
# lib/cmake/LiveKit).
get_filename_component(_lk_cmake_dir "${LiveKit_DIR}" DIRECTORY)
get_filename_component(_lk_lib_dir "${_lk_cmake_dir}" DIRECTORY)
target_link_directories(livekit_spike PRIVATE "${_lk_lib_dir}")

if(WIN32)
  get_filename_component(_lk_prefix "${_lk_lib_dir}" DIRECTORY)
  set(_lk_bin_dir "${_lk_prefix}/bin")
  foreach(_lk_runtime_dll IN ITEMS livekit.dll livekit_ffi.dll)
    if(EXISTS "${_lk_bin_dir}/${_lk_runtime_dll}")
      add_custom_command(TARGET livekit_spike POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_lk_bin_dir}/${_lk_runtime_dll}"
                "$<TARGET_FILE_DIR:livekit_spike>"
      )
    endif()
  endforeach()
endif()

option(LIVEKIT_SPIKE_BUILD_TESTS "Build the spike's pure-function unit tests" OFF)
if(LIVEKIT_SPIKE_BUILD_TESTS)
  add_subdirectory(test)
endif()
