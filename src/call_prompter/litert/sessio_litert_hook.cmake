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

# v0.19.0 pins stb itself (master dropped stb_image_resize2.h on 2026-10-19)
# but still clones zlib from its default branch. Pin zlib to the revision
# this runtime was verified with; upstream's own FetchContent call then uses
# this source instead of cloning.
include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/sessio_litert_antlr.cmake")
FetchContent_Declare(sessio_zlib
  URL https://codeload.github.com/madler/zlib/tar.gz/da607da739fa6047df13e66a2af6b8bec7c2a498
  URL_HASH SHA256=b9258cf6254e7f7c37f1cd61dba943a1c5ea3cff5718c789834dac359094f5f7
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  # Upstream adds zlib's own CMake project; only download it here.
  SOURCE_SUBDIR sessio-download-only)
FetchContent_MakeAvailable(sessio_zlib)
set(LITERTLM_ZLIB_SRC_DIR "${sessio_zlib_SOURCE_DIR}" CACHE PATH "Pinned zlib sources" FORCE)
set(FETCHCONTENT_SOURCE_DIR_ZLIB_LIB "${sessio_zlib_SOURCE_DIR}" CACHE PATH "Pinned zlib" FORCE)

function(_sessio_litert_add_library)
  if(NOT TARGET litert_lm_main)
    message(FATAL_ERROR "[Sessio] LiteRT-LM did not define litert_lm_main; cannot mirror its link line")
  endif()
  add_library(sessio_litert SHARED "${SESSIO_LITERT_SOURCE_DIR}/sessio_litert.cpp")
  include("${SESSIO_LITERT_SOURCE_DIR}/sessio_litert_llguidance.cmake")
  sessio_litert_prepare_llguidance(
    "${LITERTLM_PROJECT_ROOT}/runtime/components/constrained_decoding/llg_constraint_provider.cc"
    "${PROJECT_BINARY_DIR}/sessio_llg_constraint_provider.cc")
  target_sources(sessio_litert PRIVATE "${PROJECT_BINARY_DIR}/sessio_llg_constraint_provider.cc")
  target_include_directories(sessio_litert PRIVATE "${PROJECT_BINARY_DIR}/third_party/llguidance/parser")
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
  # v0.19's package aggregates carry their archive payload as one
  # space-separated string in INTERFACE_LINK_LIBRARIES. litert_lm_main
  # survives it because the shell splits its unquoted link line, but make's
  # prerequisite tracking for sessio_litert sees one nonexistent giant path.
  # Normalize every aggregate interface into a proper CMake list.
  foreach(_agg IN ITEMS
      LiteRTLM::Aggregate
      LiteRTLM::absl::absl
      LiteRTLM::flatbuffers::flatbuffers
      LiteRTLM::litert::litert
      LiteRTLM::protobuf::libprotobuf
      LiteRTLM::re2::re2
      LiteRTLM::sentencepiece::sentencepiece
      LiteRTLM::tflite::tflite
      LiteRTLM::tokenizers::tokenizers)
    if(TARGET ${_agg})
      get_target_property(_payload ${_agg} INTERFACE_LINK_LIBRARIES)
      if(_payload)
        string(REPLACE " " ";" _payload_list "${_payload}")
        set_target_properties(${_agg} PROPERTIES INTERFACE_LINK_LIBRARIES "${_payload_list}")
      endif()
    endif()
  endforeach()
  set(_expanded_libs "")
  foreach(_lib IN LISTS _libraries)
    string(REPLACE " " ";" _lib_parts "${_lib}")
    list(APPEND _expanded_libs ${_lib_parts})
  endforeach()
  target_link_libraries(sessio_litert PRIVATE ${_expanded_libs})
  # The embedding engine (rag-core) is not on litert_lm_main's link line.
  if(NOT TARGET LiteRTLM::Runtime::Core::EmbeddingEngineImpl)
    message(FATAL_ERROR "[Sessio] LiteRT-LM embedding engine targets are missing")
  endif()
  # v0.19's CMake port forgot this header-only Bazel target; the embedding
  # executor pulls it in transitively and the link fails with -l<name>.
  if(NOT TARGET runtime_util_litert_status_util)
    add_library(runtime_util_litert_status_util INTERFACE IMPORTED GLOBAL)
    set_target_properties(runtime_util_litert_status_util PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${LITERTLM_PROJECT_ROOT}")
  endif()
  target_link_libraries(sessio_litert PRIVATE
    LiteRTLM::Runtime::Core::EmbeddingEngineImpl
    LiteRTLM::Runtime::Engine::EmbeddingEngineSettings)
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
