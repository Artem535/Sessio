# Local call prompter runtime: libsessio_litert, built separately from
# LiteRT-LM's sources by scripts/build-litert-lm.sh (a multi-hour super-build).
# Without it the prompter is compiled with a stub that reports the runtime as
# missing; everything else in Sessio is unaffected.
set(SESSIO_LITERT_DIR "" CACHE PATH "Install prefix produced by scripts/build-litert-lm.sh")
set(SESSIO_LITERTLM_FOUND OFF)

if(NOT SESSIO_LITERT_DIR)
  message(STATUS "Call prompter runtime: not configured (set SESSIO_LITERT_DIR)")
  return()
endif()

find_library(SESSIO_LITERT_LIBRARY NAMES sessio_litert PATHS "${SESSIO_LITERT_DIR}/lib" "${SESSIO_LITERT_DIR}/bin"
             NO_DEFAULT_PATH)
if(NOT SESSIO_LITERT_LIBRARY)
  message(FATAL_ERROR "SESSIO_LITERT_DIR=${SESSIO_LITERT_DIR} has no sessio_litert library; run scripts/build-litert-lm.sh")
endif()

add_library(sessio::litert_lm SHARED IMPORTED)
if(WIN32)
  find_file(SESSIO_LITERT_DLL sessio_litert.dll PATHS "${SESSIO_LITERT_DIR}/bin" NO_DEFAULT_PATH REQUIRED)
  set_target_properties(sessio::litert_lm PROPERTIES
    IMPORTED_LOCATION "${SESSIO_LITERT_DLL}"
    IMPORTED_IMPLIB "${SESSIO_LITERT_LIBRARY}")
else()
  set_target_properties(sessio::litert_lm PROPERTIES IMPORTED_LOCATION "${SESSIO_LITERT_LIBRARY}")
endif()
set_target_properties(sessio::litert_lm PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/src/call_prompter/litert")
set(SESSIO_LITERTLM_FOUND ON)
if(UNIX AND NOT APPLE)
  file(GLOB SESSIO_LITERT_EXTRA_LIBS "${SESSIO_LITERT_DIR}/lib/libkissfft*.so*")
endif()
message(STATUS "Call prompter runtime: ${SESSIO_LITERT_LIBRARY}")
