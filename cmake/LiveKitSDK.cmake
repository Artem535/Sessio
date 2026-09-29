# Vendored from livekit-examples/cpp-example-collection
# (commit 7abd4b97aebd33a681cfbbed9b739fdc5c2c17e3, Apache 2.0). Do not
# hand-edit beyond adjusting default arguments — see spikes/livekit-cpp-spike
# on branch spike/77-livekit-cpp-spike for the original, proven copy.

function(livekit_sdk_setup)
  set(options NO_DOWNLOAD)
  set(oneValueArgs VERSION SDK_DIR REPO SHA256 TRIPLE DOWNLOAD_DIR GITHUB_TOKEN)
  cmake_parse_arguments(LK "${options}" "${oneValueArgs}" "" ${ARGN})

  if(NOT LK_VERSION)
    message(FATAL_ERROR "livekit_sdk_setup: VERSION is required")
  endif()
  if(NOT LK_SDK_DIR)
    message(FATAL_ERROR "livekit_sdk_setup: SDK_DIR is required")
  endif()
  if(NOT LK_REPO)
    set(LK_REPO "livekit/client-sdk-cpp")
  endif()

  if(NOT LK_TRIPLE)
    if(WIN32)
      set(_lk_os "windows")
    elseif(APPLE)
      set(_lk_os "macos")
    else()
      set(_lk_os "linux")
    endif()

    set(_lk_arch "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    if(_lk_arch MATCHES "^(x86_64|amd64|AMD64)$")
      set(_lk_arch "x64")
    elseif(_lk_arch MATCHES "^(arm64|aarch64|ARM64)$")
      set(_lk_arch "arm64")
    endif()

    set(LK_TRIPLE "${_lk_os}-${_lk_arch}")
  endif()

  set(_resolved_version "${LK_VERSION}")
  if(WIN32)
    set(_ext "zip")
  else()
    set(_ext "tar.gz")
  endif()

  set(_archive "livekit-sdk-${LK_TRIPLE}-${_resolved_version}.${_ext}")
  set(_url "https://github.com/${LK_REPO}/releases/download/v${_resolved_version}/${_archive}")
  set(_extracted_root "${LK_SDK_DIR}/livekit-sdk-${LK_TRIPLE}-${_resolved_version}")

  if(NOT LK_NO_DOWNLOAD)
    file(MAKE_DIRECTORY "${LK_SDK_DIR}")
    set(_archive_path "${LK_SDK_DIR}/${_archive}")

    if(NOT EXISTS "${_extracted_root}/lib/cmake")
      message(STATUS "livekit_sdk_setup: downloading ${_url}")
      if(LK_SHA256)
        file(DOWNLOAD "${_url}" "${_archive_path}" SHOW_PROGRESS TLS_VERIFY ON
             EXPECTED_HASH "SHA256=${LK_SHA256}" STATUS _st LOG _log)
      else()
        message(WARNING "livekit_sdk_setup: no SHA256 given for ${LK_TRIPLE} v${_resolved_version} — "
                         "downloaded archive integrity will not be verified")
        file(DOWNLOAD "${_url}" "${_archive_path}" SHOW_PROGRESS TLS_VERIFY ON STATUS _st LOG _log)
      endif()
      list(GET _st 0 _st_code)
      if(NOT _st_code EQUAL 0)
        message(FATAL_ERROR "livekit_sdk_setup: download failed (${_st}): ${_log}")
      endif()

      file(ARCHIVE_EXTRACT INPUT "${_archive_path}" DESTINATION "${LK_SDK_DIR}")

      if(NOT EXISTS "${_extracted_root}/lib/cmake")
        message(FATAL_ERROR "livekit_sdk_setup: expected '${_extracted_root}/lib/cmake' after extraction, not found")
      endif()
    endif()
  endif()

  list(PREPEND CMAKE_PREFIX_PATH "${_extracted_root}")
  set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
  set(LiveKit_DIR "${_extracted_root}/lib/cmake/LiveKit" PARENT_SCOPE)
  # _extracted_root itself is local to this function and does not escape to
  # the caller's scope, so export the one piece of it that Linux packaging
  # needs (the SDK's own lib/ directory, for vendoring its runtime deps —
  # see cmake/BundleLiveKitLinuxDeps.cmake) explicitly, rather than having
  # callers reach into this function's internal variable naming.
  set(LIVEKIT_SDK_LIB_DIR "${_extracted_root}/lib" PARENT_SCOPE)
endfunction()
