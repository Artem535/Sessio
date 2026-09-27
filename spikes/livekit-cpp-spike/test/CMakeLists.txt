# Throwaway spike for issue #77 — not production quality.
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz
)
FetchContent_MakeAvailable(googletest)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Multimedia)

add_executable(audio_chunker_tests
  audio_chunker_tests.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio_chunker.cpp
)
target_include_directories(audio_chunker_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(audio_chunker_tests PRIVATE gtest gtest_main)

add_executable(frame_convert_tests
  frame_convert_tests.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/frame_convert.cpp
)
target_include_directories(frame_convert_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(frame_convert_tests PRIVATE
  gtest gtest_main
  LiveKit::livekit
  Qt6::Core
  Qt6::Gui
)

# Not a GoogleTest — see the file's own header comment. Own main(), own
# QCoreApplication (needed for the worker thread's event loop / QThread::wait()
# to make sense at all), run manually: ./video_capture_adapter_smoke_test
add_executable(video_capture_adapter_smoke_test
  video_capture_adapter_smoke_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/video_capture_adapter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/video_capture_worker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/frame_convert.cpp
)
target_include_directories(video_capture_adapter_smoke_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(video_capture_adapter_smoke_test PRIVATE
  LiveKit::livekit
  Qt6::Core
  Qt6::Gui
  Qt6::Multimedia
)
