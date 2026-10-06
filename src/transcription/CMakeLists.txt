cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_transcription)

find_package(Threads REQUIRED)

add_library(${TARGET_NAME} STATIC
  transcription_types.h
)
set_target_properties(${TARGET_NAME} PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${TARGET_NAME} PUBLIC Threads::Threads)
