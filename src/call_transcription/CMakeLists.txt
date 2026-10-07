cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)

set(TARGET_NAME ${PROJECT_NAME}_call_transcription)

add_library(${TARGET_NAME} STATIC
  engine_factory.h
  engine_factory.cpp
  phrase_writer.h
  phrase_writer.cpp
  serial_executor.h
  transcription_session.h
  transcription_session.cpp
)
target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${TARGET_NAME} PUBLIC
  ${PROJECT_NAME}_transcription
  ${PROJECT_NAME}_transcription_sherpa
  ${PROJECT_NAME}_database
  ${PROJECT_NAME}_video
  Qt6::Core
)
