# Register before upstream's FetchContent declaration: its shallow clone can
# no longer reach this pinned commit after the repository's branches advance.
# Keep the exact source revision and upstream CMake patch, using a hashed archive.
include(FetchContent)
FetchContent_Declare(antlr_lib
  URL https://codeload.github.com/antlr/antlr4/tar.gz/7d5770395bb7b02eb56e7c62662cb1d7c08f42a3
  URL_HASH SHA256=39164fe39debc534a1fbcaf5b6a90a411589e49a4f38dd958fb52770016f948a
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR runtime/Cpp
  PATCH_COMMAND "${CMAKE_COMMAND}"
    -DTARGET_FILE=<SOURCE_DIR>/runtime/Cpp/CMakeLists.txt
    -P${LITERTLM_PROJECT_ROOT}/cmake/patches/antlr_patch.cmake)
