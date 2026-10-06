cmake_minimum_required(VERSION 3.28)

# Set the C++ standard
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Define the project
project(Sessio VERSION 0.2.16 LANGUAGES CXX)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Svg PrintSupport LinguistTools Multimedia MultimediaWidgets OpenGLWidgets)
include(GNUInstallDirs)
include(FetchContent)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
set(LIVEKIT_SDK_VERSION "1.12.0" CACHE STRING "LiveKit C++ SDK version")

# Pinned per-platform archive hashes for LIVEKIT_SDK_VERSION, verified by
# actually downloading and hashing that platform's release archive — see
# livekit_sdk_setup()'s EXPECTED_HASH plumbing in cmake/LiveKitSDK.cmake.
# Only linux-x64 is verified so far (this repo's dev/CI Linux runners); add
# the Windows/macOS entries here once a CI run downloads and its log's
# archive hash can be confirmed against the corresponding GitHub release
# asset. An unset entry below is not silently insecure: livekit_sdk_setup()
# emits a build-time WARNING for any triple with no hash.
#
# This hash is tied to LIVEKIT_SDK_VERSION above (currently 1.12.0):
# bumping that version invalidates it, and a fresh download at the new
# version will FATAL_ERROR on the mismatch (file(DOWNLOAD)'s EXPECTED_HASH
# behavior) rather than silently accepting an unverified archive. Update
# this value alongside any version bump.
set(_lk_sha256_linux-x64 "d7aec81b1c16df494f36bfc612dc2d52fd9c22f03340b8db90e9ad942a207730")

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
set(_lk_sha256 "${_lk_sha256_${_lk_os}-${_lk_arch}}")

include(LiveKitSDK)
livekit_sdk_setup(
  VERSION "${LIVEKIT_SDK_VERSION}"
  SDK_DIR "${CMAKE_BINARY_DIR}/_deps/livekit-sdk"
  SHA256 "${_lk_sha256}"
)
find_package(LiveKit CONFIG REQUIRED)
option(PCM_BUILD_TESTS "Build project tests" OFF)
include(cmake/Transcription.cmake)

if(Qt6_VERSION VERSION_LESS 6.8.0)
  message(FATAL_ERROR "Qlementine requires Qt 6.8.0 or newer. Detected Qt ${Qt6_VERSION}.")
endif()

# QtKeychain needs ECM's CMake modules but does not need the full ECM project.
FetchContent_Declare(
  pcm_ecm
  URL https://github.com/KDE/extra-cmake-modules/archive/refs/tags/v5.115.0.tar.gz
  URL_HASH SHA512=a3f23f74b2c14c1ae059a1c7b1f5011f484699b067903da70a44c792d20edc4f7b4f012af5189fe31de622fe227e648b4d15c8feb359bcd9d8f6fc50822a36bf
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_GetProperties(pcm_ecm)
if(NOT pcm_ecm_POPULATED)
  FetchContent_Populate(pcm_ecm)
endif()
list(PREPEND CMAKE_MODULE_PATH "${pcm_ecm_SOURCE_DIR}/modules")

# On Linux the RPM links against the distribution's own Qt and qtkeychain
# instead of bundling them: a package that ships libQt6*.so.6 (and Provides
# them) overwrites the system Qt on install. The AppImage build opts back in
# with -DSESSIO_BUNDLE_QT=ON, because it needs Qt and friends in the install
# tree for linuxdeploy.
if(UNIX AND NOT APPLE)
  option(SESSIO_BUNDLE_QT "Install Qt and bundled third-party libraries into the install tree" OFF)
else()
  set(SESSIO_BUNDLE_QT ON)
endif()
set(SESSIO_THIRD_PARTY_SUBDIR_ARGS "")
if(NOT SESSIO_BUNDLE_QT)
  set(SESSIO_THIRD_PARTY_SUBDIR_ARGS EXCLUDE_FROM_ALL)
endif()

set(BUILD_WITH_QT6 ON CACHE BOOL "" FORCE)
set(BUILD_TEST_APPLICATION OFF CACHE BOOL "" FORCE)
set(BUILD_TRANSLATIONS OFF CACHE BOOL "" FORCE)
set(LIBSECRET_SUPPORT ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  qtkeychain
  URL https://github.com/frankosterfeld/qtkeychain/archive/refs/tags/0.14.3.tar.gz
  URL_HASH SHA512=d1d87553db94bf54da1373016a847476e6cd608db6d427ed72532658e2272501daf45d7c9976efdde2f26ab3810ba9dbfec2518d46dee5a76ecaa369bfee2e4a
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  ${SESSIO_THIRD_PARTY_SUBDIR_ARGS}
)
FetchContent_MakeAvailable(qtkeychain)

set(QLEMENTINE_PATCH_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/apply_qlementine_patch.cmake")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -P "${QLEMENTINE_PATCH_SCRIPT}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  RESULT_VARIABLE QLEMENTINE_PATCH_RESULT
  OUTPUT_VARIABLE QLEMENTINE_PATCH_OUTPUT
  ERROR_VARIABLE QLEMENTINE_PATCH_ERROR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE
)

if(NOT QLEMENTINE_PATCH_RESULT EQUAL 0)
  message(FATAL_ERROR
    "Failed to apply local Qlementine patch.\n"
    "stdout: ${QLEMENTINE_PATCH_OUTPUT}\n"
    "stderr: ${QLEMENTINE_PATCH_ERROR}")
endif()

if(QLEMENTINE_PATCH_OUTPUT)
  message(STATUS "${QLEMENTINE_PATCH_OUTPUT}")
endif()

add_custom_target(apply_qlementine_patch
  COMMAND "${CMAKE_COMMAND}" -P "${QLEMENTINE_PATCH_SCRIPT}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Applying local patch to third_party/qlementine"
  VERBATIM
)

set(QLEMENTINE_SANDBOX OFF CACHE BOOL "" FORCE)
set(QLEMENTINE_SHOWCASE OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/qlementine ${SESSIO_THIRD_PARTY_SUBDIR_ARGS})

qt_add_library(qcustomplot STATIC
    third_party/qcustomplot/qcustomplot.cpp
    third_party/qcustomplot/qcustomplot.h
)
target_include_directories(qcustomplot PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party/qcustomplot
)
set_target_properties(qcustomplot PROPERTIES
    AUTOMOC ON
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(qcustomplot PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::PrintSupport
)

qt_standard_project_setup()

qt_add_translations(Sessio
    TS_FILES
        translation/app_ru.ts
        translation/app_en.ts
    LUPDATE_OPTIONS -locations none -no-obsolete
)

# Include subdirectories for modules
# Shared recurring-schedule calculation (also used by the token backend):
# series with a pinned timezone are expanded by the same code on both sides.
add_subdirectory(token-backend/schedule ${CMAKE_BINARY_DIR}/pcm_schedule)
add_subdirectory(src/token_client)
add_subdirectory(src/widgets)
add_subdirectory(src/config)
add_subdirectory(src/database)
add_subdirectory(src/meeting)
add_subdirectory(src/video)
if(SESSIO_TRANSCRIPTION_ENABLED)
  add_subdirectory(src/transcription)
endif()
add_subdirectory(src/backup)
add_subdirectory(src/client_model)
add_subdirectory(src/pages/client_info_page)
add_subdirectory(src/pages/event_info_page)
add_subdirectory(src/pages/detail_client_info_page)
add_subdirectory(src/pages/client_notes_page)
add_subdirectory(src/pages/analytics_page)
add_subdirectory(src/pages/calls_page)
add_subdirectory(src/timeline_widget)
add_subdirectory(src/event_view)
add_subdirectory(src/app)

include_directories(src/app)

if(PCM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

set(PCM_APP_SOURCES
    src/main.cpp
    resources/themes.qrc
)

if(WIN32)
  set(PCM_APP_ICON_PATH "${CMAKE_SOURCE_DIR}/resources/icons/Sessio.ico")
  configure_file(
      "${CMAKE_SOURCE_DIR}/packaging/windows/app_icon.rc.in"
      "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc"
      @ONLY
  )
  list(APPEND PCM_APP_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")
endif()

# Add the main executable
qt_add_executable(Sessio ${PCM_APP_SOURCES})
set_target_properties(Sessio PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

if(APPLE)
  # Registers the sessio:// URL scheme in the bundle's Info.plist
  # (CFBundleURLTypes) so macOS can launch/forward links to Sessio. CMake's
  # MACOSX_BUNDLE_INFO_PLIST has no first-class property for CFBundleURLTypes,
  # so this template is a copy of CMake's own default
  # (Modules/MacOSXBundleInfo.plist.in) with that array added — every other
  # key/substitution behaves exactly as before this template was introduced.
  set_target_properties(Sessio PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in"
  )
endif()

add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_app)

# Link with libs
target_link_libraries(Sessio PUBLIC ${PROJECT_NAME}_app Qt6::Widgets)

install(TARGETS Sessio
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(UNIX AND NOT APPLE)
  # Not "." — that would drop /usr/LICENSE and /usr/README.md into the prefix.
  install(FILES
      ${CMAKE_SOURCE_DIR}/LICENSE
      ${CMAKE_SOURCE_DIR}/README.md
      DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/Sessio
  )
else()
  install(FILES
      ${CMAKE_SOURCE_DIR}/LICENSE
      ${CMAKE_SOURCE_DIR}/README.md
      DESTINATION .
  )
endif()

# libical's timezone data for the recurring-schedule engine. Resolved at run
# time relative to the executable (src/meeting/schedule_zoneinfo.h): RPM and
# AppImage get <prefix>/share/sessio/zoneinfo, the macOS bundle gets
# Contents/Resources/zoneinfo. The Windows installer copies the same directory
# next to Sessio.exe (see the workflow's Windows deploy step).
if(NOT EXISTS "${PCM_SCHEDULE_ZONEINFO_SOURCE_DIR}/America/New_York.ics")
  message(FATAL_ERROR "libical timezone data missing: ${PCM_SCHEDULE_ZONEINFO_SOURCE_DIR}")
endif()
if(APPLE)
  install(DIRECTORY ${PCM_SCHEDULE_ZONEINFO_SOURCE_DIR}/
      DESTINATION Sessio.app/Contents/Resources/zoneinfo)
else()
  install(DIRECTORY ${PCM_SCHEDULE_ZONEINFO_SOURCE_DIR}/
      DESTINATION ${CMAKE_INSTALL_DATADIR}/sessio/zoneinfo)
endif()

if(UNIX AND NOT APPLE)
  install(FILES
      ${CMAKE_SOURCE_DIR}/packaging/Sessio.desktop
      DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
  )
  install(FILES
      ${CMAKE_SOURCE_DIR}/resources/icons/brain-solid-full.svg
      DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
      RENAME Sessio.svg
  )
endif()

if(SESSIO_BUNDLE_QT)
  qt_generate_deploy_app_script(
      TARGET Sessio
      OUTPUT_SCRIPT deploy_runtime_dependencies_script
      NO_UNSUPPORTED_PLATFORM_ERROR
  )
  install(SCRIPT ${deploy_runtime_dependencies_script})
endif()

if(UNIX AND NOT APPLE)
  # The prebuilt Linux LiveKit SDK archive bundles its own libcurl.so.4,
  # which itself needs ~20 further libraries (OpenSSL, GnuTLS, Kerberos,
  # LDAP, etc). qt_generate_deploy_app_script() above only walks Sessio's
  # OWN direct runtime deps (Qt/qlementine/qt6keychain plus liblivekit.so/
  # liblivekit_ffi.so themselves as direct link deps) — it does not recurse
  # into what liblivekit.so itself needs. Vendor that whole chain into a
  # private, RPATH-scoped subdirectory so plain `cpack --generator RPM`
  # packaging does not silently fall back to the host distro's own
  # curl/openssl/gnutls/krb5/openldap, which this SDK was never built or
  # tested against, and does not drop a second libcurl.so.4 at a path
  # Fedora's own curl package already owns. Linux-only: this problem is
  # specific to the prebuilt Linux SDK archive's bundled curl; the
  # AppImage step already handles this correctly via its own linuxdeploy
  # scan and is untouched by this.
  set(SESSIO_PRIVATE_LIBDIR_NAME "sessio")
  install(CODE "
    set(LIVEKIT_SDK_LIB_DIR \"${LIVEKIT_SDK_LIB_DIR}\")
    set(SESSIO_INSTALL_LIBDIR \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}\")
    set(SESSIO_INSTALL_BINDIR \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
    set(SESSIO_PRIVATE_LIBDIR_NAME \"${SESSIO_PRIVATE_LIBDIR_NAME}\")
    include(\"${CMAKE_SOURCE_DIR}/cmake/BundleLiveKitLinuxDeps.cmake\")
  ")
endif()

set(CPACK_PACKAGE_NAME "Sessio")
set(CPACK_PACKAGE_VENDOR "Sessio")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}"
)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")

if(WIN32)
  set(CPACK_GENERATOR "ZIP")
elseif(UNIX AND NOT APPLE)
  set(CPACK_GENERATOR "RPM")
  set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
  set(CPACK_RPM_PACKAGE_RELEASE 1)
  set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
  set(CPACK_RPM_PACKAGE_GROUP "Applications/Productivity")
  set(CPACK_RPM_PACKAGE_URL "https://github.com/Artem535/Sessio")
  set(CPACK_RPM_PACKAGE_DESCRIPTION
      "Local-first desktop workspace for calendar scheduling, client management, and session tracking.")
  # Qt and qtkeychain come from the distribution; nothing Qt-related is
  # bundled (see SESSIO_BUNDLE_QT). Exact sonames/versions are added by rpm's
  # automatic dependency generator on top of these.
  set(CPACK_RPM_PACKAGE_REQUIRES
      "qt6-qtbase >= 6.8, qt6-qtbase-gui, qt6-qtsvg, qt6-qtwayland, qt6-qtmultimedia, qtkeychain-qt6")
  # The private /usr/lib/sessio directory (LiveKit and its curl/OpenSSL/...
  # chain) must neither advertise Provides for those sonames — that would
  # shadow the system packages — nor generate Requires for them.
  set(CPACK_RPM_SPEC_MORE_DEFINE
"%global __provides_exclude_from ^/usr/lib(64)?/sessio/.*$
%global __requires_exclude_from ^/usr/lib(64)?/sessio/.*$
%global __requires_exclude ^liblivekit(_ffi)?\\.so")
  # Registers Sessio.desktop (via its MimeType=x-scheme-handler/sessio entry)
  # as the sessio:// URL handler and refreshes the desktop database on
  # install; see packaging/linux/register-sessio-scheme.sh. Uninstall uses a
  # separate script that only refreshes the desktop database — it must NOT
  # re-run the same xdg-mime registration, which would point the system
  # default handler at a .desktop file that was just removed; see
  # packaging/linux/unregister-sessio-scheme.sh.
  set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE
      "${CMAKE_SOURCE_DIR}/packaging/linux/register-sessio-scheme.sh")
  set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE
      "${CMAKE_SOURCE_DIR}/packaging/linux/unregister-sessio-scheme.sh")
endif()

include(CPack)
