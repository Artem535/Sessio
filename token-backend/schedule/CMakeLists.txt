find_package(LibIcal CONFIG REQUIRED)
set(PCM_SCHEDULE_ZONEINFO_DIR "${LibIcal_DIR}/../libical/zoneinfo" CACHE PATH
    "Runtime directory of the libical builtin timezone data (deploy the same files on both peers)")
add_library(pcm_schedule STATIC schedule.cpp)
target_compile_features(pcm_schedule PUBLIC cxx_std_20)
target_include_directories(pcm_schedule PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(pcm_schedule PUBLIC ical)
target_compile_definitions(pcm_schedule PRIVATE PCM_SCHEDULE_ZONEINFO_DIR="${PCM_SCHEDULE_ZONEINFO_DIR}")
