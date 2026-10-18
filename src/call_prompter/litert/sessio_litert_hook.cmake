# Included into LiteRT-LM's own CMake build (its "litert_lm" phase) through
# CMAKE_PROJECT_LiteRT-LM_INCLUDE by scripts/build-litert-lm.sh. At the end of
# that configure step it adds libsessio_litert: sessio_litert.cpp linked
# exactly like LiteRT-LM's litert_lm_main, with every symbol hidden except
# Sessio's sessio::litert interface.

# The upstream super-build flattens LITERTLM_TOOLCHAIN_ARGS when forwarding it
# to this project, retaining only its first element in the child cache.
# Re-establish the settings here before any further ExternalProject is added.
list(APPEND LITERTLM_TOOLCHAIN_ARGS
  "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
  "-DCMAKE_PROJECT_LiteRT_INCLUDE=${CMAKE_CURRENT_LIST_DIR}/sessio_litert_cpu.cmake")
list(REMOVE_DUPLICATES LITERTLM_TOOLCHAIN_ARGS)
set(LITERTLM_TOOLCHAIN_ARGS "${LITERTLM_TOOLCHAIN_ARGS}" CACHE STRING
  "Compiler and LiteRT compatibility settings for external builds" FORCE)
# Revisions are pinned by the build script. Re-running a build must not reset
# patched dependency sources and needlessly invalidate all compiled objects.
set_property(DIRECTORY PROPERTY EP_UPDATE_DISCONNECTED TRUE)

function(_sessio_litert_add_library)
  if(NOT TARGET litert_lm_main)
    message(FATAL_ERROR "[Sessio] LiteRT-LM did not define litert_lm_main; cannot mirror its link line")
  endif()
  add_library(sessio_litert SHARED "${SESSIO_LITERT_SOURCE_DIR}/sessio_litert.cpp")
  if(TARGET litertlm_anchor)
    add_dependencies(sessio_litert litertlm_anchor)
  endif()
  get_target_property(_includes litert_lm_main INCLUDE_DIRECTORIES)
  get_target_property(_libraries litert_lm_main LINK_LIBRARIES)
  # Upstream expands every vendor archive into the main program's link line.
  # Match the CPU build's exclusion of Samsung's optional NPU subtree.
  list(FILTER _libraries EXCLUDE REGEX "[/\\\\]vendors[/\\\\]samsung[/\\\\]")
  set_property(TARGET litert_lm_main PROPERTY LINK_LIBRARIES "${_libraries}")
  # Only the command-line tool needs Python; the runtime does not.
  list(REMOVE_ITEM _libraries Python3::Python)
  target_include_directories(sessio_litert PRIVATE ${_includes} "${SESSIO_LITERT_SOURCE_DIR}")
  target_link_libraries(sessio_litert PRIVATE ${_libraries})
  target_compile_definitions(sessio_litert PRIVATE SESSIO_LITERT_BUILD)
  set_target_properties(sessio_litert PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
  if(UNIX AND NOT APPLE)
    target_link_options(sessio_litert PRIVATE
      "LINKER:--version-script=${SESSIO_LITERT_SOURCE_DIR}/sessio_litert.map"
      "LINKER:--exclude-libs,ALL"
      "LINKER:-z,defs")
  endif()
  install(TARGETS sessio_litert LIBRARY DESTINATION lib RUNTIME DESTINATION bin ARCHIVE DESTINATION lib)
endfunction()

if(LITERTLM_BUILD_TARGETS)
  cmake_language(DEFER CALL _sessio_litert_add_library)
endif()
