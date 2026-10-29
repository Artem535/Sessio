# LiteRT-LM v0.18.0 replaces LiteRT's root CMakeLists.txt with its shim,
# omitting SafeInt's download while core/model still includes <build>/safeint.
# Restore the header-only dependency at the revision pinned by LiteRT itself.
include(FetchContent)
FetchContent_Declare(sessio_safeint
  URL https://codeload.github.com/dcleblanc/SafeInt/tar.gz/72f0745e26091af6d1186fd91426377f16688ba7
  URL_HASH SHA256=4bd737e5885112e796742b7b6ba7181ff346855dc22d29ed2b258de567a1de8d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_DIR "${CMAKE_BINARY_DIR}/safeint"
  # Only headers are required; do not add SafeInt's own test/build targets.
  SOURCE_SUBDIR sessio-header-only)
FetchContent_MakeAvailable(sessio_safeint)
if(NOT EXISTS "${sessio_safeint_SOURCE_DIR}/SafeInt.hpp")
  message(FATAL_ERROR "[Sessio] SafeInt download did not provide SafeInt.hpp")
endif()
include_directories("${sessio_safeint_SOURCE_DIR}")
