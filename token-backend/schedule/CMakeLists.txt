find_package(LibIcal CONFIG REQUIRED)
set(PCM_SCHEDULE_ZONEINFO_DIR "${LibIcal_DIR}/../libical/zoneinfo" CACHE PATH
    "Runtime directory of the libical builtin timezone data (deploy the same files on both peers)")
# The path is compiled in and read at run time, so a configure-time default that
# points into a build tree is not portable. Fail early if it does not exist and
# make deployments pass an explicit, installed path (see token-backend/README.md,
# "Timezone data").
if(NOT EXISTS "${PCM_SCHEDULE_ZONEINFO_DIR}/America/New_York" AND
   NOT EXISTS "${PCM_SCHEDULE_ZONEINFO_DIR}/America/New_York.ics")
  message(FATAL_ERROR
    "PCM_SCHEDULE_ZONEINFO_DIR='${PCM_SCHEDULE_ZONEINFO_DIR}' does not contain the "
    "libical timezone data. Pass -DPCM_SCHEDULE_ZONEINFO_DIR=<dir> (the vcpkg "
    "share/libical/zoneinfo directory) and ship the same directory at that path.")
endif()
add_library(pcm_schedule STATIC schedule.cpp)
target_compile_features(pcm_schedule PUBLIC cxx_std_20)
target_include_directories(pcm_schedule PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(pcm_schedule PUBLIC ical)
target_compile_definitions(pcm_schedule PRIVATE PCM_SCHEDULE_ZONEINFO_DIR="${PCM_SCHEDULE_ZONEINFO_DIR}")
