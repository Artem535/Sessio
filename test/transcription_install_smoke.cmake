# Checks that an install tree contains everything the transcription module needs.
# Usage: cmake -DINSTALL_ROOT=<prefix> -P test/transcription_install_smoke.cmake
# Linux layout only (models in share/sessio/models, libraries in lib*/sessio).
if(NOT INSTALL_ROOT)
  message(FATAL_ERROR "INSTALL_ROOT is required")
endif()

foreach(_rel
    share/sessio/models/silero_vad.onnx
    share/sessio/models/gigaam-v3-rnnt/encoder.int8.onnx
    share/sessio/models/gigaam-v3-rnnt/decoder.onnx
    share/sessio/models/gigaam-v3-rnnt/joiner.onnx
    share/sessio/models/gigaam-v3-rnnt/tokens.txt)
  if(NOT EXISTS "${INSTALL_ROOT}/${_rel}")
    message(FATAL_ERROR "Missing from install tree: ${_rel}")
  endif()
endforeach()

file(GLOB_RECURSE _libs "${INSTALL_ROOT}/lib*/sessio/libsherpa-onnx-cxx-api.so*")
file(GLOB_RECURSE _ort "${INSTALL_ROOT}/lib*/sessio/libonnxruntime.so*")
if(NOT _libs OR NOT _ort)
  message(FATAL_ERROR "sherpa-onnx or ONNX Runtime libraries missing from the install tree")
endif()
# Installed sherpa libs must not point back into the build tree.
find_program(_readelf readelf)
if(_readelf)
  file(GLOB_RECURSE _sherpa "${INSTALL_ROOT}/lib*/sessio/libsherpa-onnx-*.so")
  foreach(_lib IN LISTS _sherpa)
    execute_process(COMMAND "${_readelf}" -d "${_lib}" OUTPUT_VARIABLE _dyn)
    string(REGEX MATCH "(RPATH|RUNPATH)[^\n]*" _rp "${_dyn}")
    if(_rp AND NOT _rp MATCHES "\\[\\$ORIGIN\\]$")
      message(FATAL_ERROR "Unexpected RUNPATH on installed ${_lib}: ${_rp}")
    endif()
  endforeach()
else()
  message(WARNING "readelf not found, RUNPATH check skipped")
endif()
message(STATUS "Transcription install tree OK")
