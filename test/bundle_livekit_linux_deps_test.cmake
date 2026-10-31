if(NOT DEFINED PCM_SOURCE_DIR OR NOT DEFINED PCM_TEST_BINARY_DIR)
  message(FATAL_ERROR "PCM_SOURCE_DIR and PCM_TEST_BINARY_DIR must be set")
endif()

if(NOT UNIX OR APPLE)
  message(STATUS "Skipping Linux-only LiveKit bundle staging test")
  return()
endif()

find_program(_patchelf patchelf)
if(NOT _patchelf)
  message(FATAL_ERROR "patchelf is required for the LiveKit bundle staging test")
endif()

find_file(_fixture_library NAMES libz.so.1
  PATHS /lib64 /usr/lib64 /lib /usr/lib
  PATH_SUFFIXES x86_64-linux-gnu aarch64-linux-gnu
  REQUIRED)
find_program(_fixture_binary true REQUIRED)

set(_test_root "${PCM_TEST_BINARY_DIR}/bundle-livekit-linux-deps")
set(_logical_prefix "${_test_root}/logical-prefix")
set(_stage_root "${_test_root}/stage")
set(_sdk_lib_dir "${_test_root}/sdk/lib")
file(REMOVE_RECURSE "${_test_root}")
file(MAKE_DIRECTORY "${_sdk_lib_dir}" "${_stage_root}${_logical_prefix}/bin")

# A real ELF shared library keeps this test at the install-script seam: CMake
# resolves its runtime dependencies and patchelf updates the staged copy just
# as it does for the LiveKit SDK during RPM packaging.
file(COPY "${_fixture_library}" DESTINATION "${_sdk_lib_dir}" FOLLOW_SYMLINK_CHAIN)
file(COPY_FILE "${_fixture_binary}" "${_stage_root}${_logical_prefix}/bin/Sessio")

set(ENV{DESTDIR} "${_stage_root}")
set(LIVEKIT_SDK_LIB_DIR "${_sdk_lib_dir}")
set(SESSIO_INSTALL_LIBDIR "${_logical_prefix}/lib")
set(SESSIO_INSTALL_BINDIR "${_logical_prefix}/bin")
set(SESSIO_PRIVATE_LIBDIR_NAME "sessio")
include("${PCM_SOURCE_DIR}/cmake/BundleLiveKitLinuxDeps.cmake")

file(GLOB _staged_own_libs "${_stage_root}${_logical_prefix}/lib/sessio/libz.so*")
if(NOT _staged_own_libs)
  message(FATAL_ERROR "LiveKit dependencies were not copied into the DESTDIR staging tree")
endif()
if(EXISTS "${_logical_prefix}/lib/sessio")
  message(FATAL_ERROR "LiveKit dependencies escaped DESTDIR into the logical install prefix")
endif()
if(EXISTS "${_stage_root}${_logical_prefix}/lib/sessio/libc.so.6")
  message(FATAL_ERROR "The system C runtime must not be bundled with LiveKit dependencies")
endif()

execute_process(
  COMMAND "${_patchelf}" --print-rpath "${_stage_root}${_logical_prefix}/bin/Sessio"
  OUTPUT_VARIABLE _rpath
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _rpath_result
)
if(NOT _rpath_result EQUAL 0 OR NOT _rpath MATCHES "\\$ORIGIN/\\.\\./lib/sessio")
  message(FATAL_ERROR "The staged Sessio binary did not receive the private LiveKit RPATH")
endif()
