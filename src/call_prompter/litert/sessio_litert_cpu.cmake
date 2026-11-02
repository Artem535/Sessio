# Compatibility fixes for LiteRT-LM v0.18.0's LiteRT CMake shim.
include("${CMAKE_CURRENT_LIST_DIR}/sessio_litert_safeint.cmake")

# The shim imports TFLite's archives, but leaves the plain `farmhash` link
# dependency unresolved. Use the archive built by that same TFLite build.
if(NOT TARGET farmhash)
  set(_sessio_farmhash "${LITERTLM_TFLITE_BUILD_DIR}/_deps/farmhash-build/libfarmhash${CMAKE_STATIC_LIBRARY_SUFFIX}")
  if(NOT EXISTS "${_sessio_farmhash}")
    message(FATAL_ERROR "[Sessio] TFLite farmhash archive is missing: ${_sessio_farmhash}")
  endif()
  add_library(farmhash STATIC IMPORTED GLOBAL)
  set_target_properties(farmhash PROPERTIES IMPORTED_LOCATION "${_sessio_farmhash}")
endif()

function(_sessio_litert_exclude_samsung)
  # The CPU prompter does not use Samsung NPU plugins. GCC 15.2 crashes on
  # AiLiteCoreManager::PublicApi, even when compiling that file on its own.
  # Keep the targets defined, but omit this optional subtree from `all`.
  if(TARGET samsung_ai_litecore_manager)
    set_property(DIRECTORY "${CMAKE_SOURCE_DIR}/vendors/samsung"
      PROPERTY EXCLUDE_FROM_ALL TRUE)
  endif()
endfunction()
cmake_language(DEFER CALL _sessio_litert_exclude_samsung)
