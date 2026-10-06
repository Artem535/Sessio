cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_transcription)

find_package(Threads REQUIRED)

add_library(${TARGET_NAME} STATIC
  transcription_types.h
  resampler.h
  resampler.cpp
  model_locator.h
  model_locator.cpp
  voice_activity.h
  speech_recognizer.h
  phrase_segmenter.h
  phrase_segmenter.cpp
  transcription_engine.h
  transcription_engine.cpp
)
target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${TARGET_NAME} PUBLIC Threads::Threads)
