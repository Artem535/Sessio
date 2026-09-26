cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME ${PROJECT_NAME}_video)

find_package(Qt6 REQUIRED COMPONENTS Core Multimedia MultimediaWidgets OpenGLWidgets)

qt_add_library(${TARGET_NAME} STATIC
        video_provider_kind.h
)

target_link_libraries(${TARGET_NAME} PUBLIC
        Qt6::Core
        Qt6::Multimedia
        Qt6::MultimediaWidgets
        Qt6::OpenGLWidgets
        LiveKit::livekit
)

target_include_directories(${TARGET_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

get_filename_component(_lk_cmake_dir "${LiveKit_DIR}" DIRECTORY)
get_filename_component(_lk_lib_dir "${_lk_cmake_dir}" DIRECTORY)
target_link_directories(${TARGET_NAME} PUBLIC "${_lk_lib_dir}")

if(WIN32)
  get_filename_component(_lk_prefix "${_lk_lib_dir}" DIRECTORY)
  set(_lk_bin_dir "${_lk_prefix}/bin")
  foreach(_lk_runtime_dll IN ITEMS livekit.dll livekit_ffi.dll)
    if(EXISTS "${_lk_bin_dir}/${_lk_runtime_dll}")
      add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_lk_bin_dir}/${_lk_runtime_dll}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>")
    endif()
  endforeach()
endif()
