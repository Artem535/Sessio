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
message(STATUS "Transcription install tree OK")
