find_package(LibIcal CONFIG REQUIRED)
# libical's builtin timezone data as available at BUILD time (vcpkg). It is
# always validated: a build without it can not resolve any recurrence.
set(PCM_SCHEDULE_ZONEINFO_SOURCE_DIR "${LibIcal_DIR}/../libical/zoneinfo" CACHE PATH
    "Build-time directory of the libical builtin timezone data (validated at configure time)")
# Directory compiled into the binary and read at RUN time. It may legitimately
# differ from the source dir (e.g. the Docker image copies the data to
# /usr/share/pcm-schedule/zoneinfo in the runtime stage), so it is only checked
# with a warning. Deploy the same files on both peers.
set(PCM_SCHEDULE_ZONEINFO_DIR "${PCM_SCHEDULE_ZONEINFO_SOURCE_DIR}" CACHE PATH
    "Runtime directory of the libical builtin timezone data compiled into the binary")
if(NOT EXISTS "${PCM_SCHEDULE_ZONEINFO_SOURCE_DIR}/America/New_York.ics" AND
   NOT EXISTS "${PCM_SCHEDULE_ZONEINFO_SOURCE_DIR}/America/New_York")
  message(FATAL_ERROR
    "libical timezone data not found in PCM_SCHEDULE_ZONEINFO_SOURCE_DIR="
    "'${PCM_SCHEDULE_ZONEINFO_SOURCE_DIR}'. Pass -DPCM_SCHEDULE_ZONEINFO_SOURCE_DIR=<vcpkg share/libical/zoneinfo>.")
endif()
if(NOT EXISTS "${PCM_SCHEDULE_ZONEINFO_DIR}/America/New_York.ics" AND
   NOT EXISTS "${PCM_SCHEDULE_ZONEINFO_DIR}/America/New_York")
  message(WARNING
    "PCM_SCHEDULE_ZONEINFO_DIR='${PCM_SCHEDULE_ZONEINFO_DIR}' has no timezone data at "
    "configure time. That is fine only if the data is installed at exactly that path "
    "on the machine that runs the binary (e.g. copied in a Docker runtime stage).")
endif()
add_library(pcm_schedule STATIC schedule.cpp)
target_compile_features(pcm_schedule PUBLIC cxx_std_20)
target_include_directories(pcm_schedule PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(pcm_schedule PUBLIC ical)
target_compile_definitions(pcm_schedule PRIVATE PCM_SCHEDULE_ZONEINFO_DIR="${PCM_SCHEDULE_ZONEINFO_DIR}")
